// Copyright 2007 Georgia Institute of Technology. All rights reserved.
// ABSOLUTELY NOT FOR DISTRIBUTION
/**
 * @file kdtree.h
 *
 * Tools for kd-trees.
 *
 * @experimental
 */

#ifndef TREE_SPKDTREE_H
#define TREE_SPKDTREE_H

#include "spnode.h"
#include "spbounds.h"

#include "base/common.h"
#include "col/arraylist.h"
#include "file/serialize.h"
#include "fx/fx.h"

/* Implementation */

template<typename TPointInfo, typename TNode, typename TParam>
class KdTreeMidpointBuilder {
 public:
  typedef TPointInfo PointInfo;
  typedef TNode Node;
  typedef typename TNode::Bound Bound;
  typedef TParam Param;
  
 private:
  const Param* param_;
  CacheArray<Vector> points_;
  CacheArray<PointInfo> point_infos_;
  CacheArray<Node> nodes_;
  CacheArrayAllocator allocator_;
  index_t leaf_size_;
  index_t dim_;
  
 public:
  void InitBuild(
      struct datanode *module,
      const Param* param_in_,
      CacheArray<Vector> *points_in,
      CacheArray<PointInfo> *point_infos_in,
      CacheArray<Node> *nodes_inout) {
    param_ = param_in_;

    points_.Init(points_in, BlockDevice::MODIFY);
    point_infos_.Init(nodes_, BlockDevice::MODIFY);
    nodes_.Init(nodes_inout_, BlockDevice::CREATE);
    allocator_.Init(&nodes_);

    const Vector *first_point = points_.StartRead(points_.begin_index());
    dim_ = first_point->length();
    points_.StopRead(points_.begin_index());
    
    leaf_size_ = fx_param_int(module, "leaf_size", 20);

    Build_();
    
    points_.Flush();
    point_infos_.Flush();
    nodes_.Flush();
  }
  
 private:
  index_t Partition_(
      index_t split_dim, double splitvalue,
      index_t first, index_t count,
      Bound* left_bound, Bound* right_bound);
  void FindBoundingBox_(index_t first, index_t count, Bound *bound);
  void Build_(index_t node_i);
  void Build_();
};

template<typename TPointInfo, typename TNode, typename TParam>
void KdTreeMidpointBuilder<TPointInfo, TNode, TParam>::FindBoundingBox_(
    index_t first, index_t count, Bound *bound) {
  index_t end = first + count;
  for (index_t i = first; i < end; i++) {
    const Vector *v = points_.StartRead(i);
    *bound |= *v;
    points_.StopRead(i);
  }
}
template<typename TPointInfo, typename TNode, typename TParam>
index_t KdTreeMidpointBuilder<TPointInfo, TNode, TParam>::Partition_(
    index_t split_dim, double splitvalue,
    index_t first, index_t count,
    Bound* left_bound, Bound* right_bound) {
  index_t left = first;
  index_t right = first + count - 1;

  /* At any point:
   *
   *   everything < left is correct
   *   everything > right is correct
   */
  for (;;) {
    Vector *left_v;
    Vector *right_v;

    while (1) {
      left_v = points_.StartWrite(left);
      if (left_v->get(split_dim) >= splitvalue || unlikely(left > right)) {
        break;
      }
      *left_bound |= *left_v;;
      left++;
    }

    while (1)
      right_v = points_.StartWrite(right);
      if (right_v->get(split_dim) < splitvalue || unlikely(left > right)) {
        break;
      }
      *right_bound |= *right_v;
      right--;
    }

    if (unlikely(left > right)) {
      /* left == right + 1 */
      break;
    }

    *left_v.SwapValues(right_v);
    // TODO: If point info has pointers this will incur bad cache performance
    // In the future we rely on OT frozen storage
    point_infos_.Swap(left, right);

    *left_bound |= *left_v;
    *right_bound |= *right_v;

    //index_t t = old_from_new_indices_[left];
    //old_from_new_indices_[left] = old_from_new_indices_[right];
    //old_from_new_indices_[right] = t;

    DEBUG_ASSERT(left <= right);
    right--;
  }

  DEBUG_ASSERT(left == right + 1);

  return left;
}

template<typename TPointInfo, typename TNode, typename TParam>
void KdTreeMidpointBuilder<TPointInfo, TNode, TParam>::KdTreeMidpointBuilder::Build_(
    index_t node_i) {
  Node *node = nodes_.StartWrite(node_i);
  bool leaf;
  
  node->stat().Init(*param_);

  if (node->count() > leaf_size_) {
    index_t split_dim = BIG_BAD_NUMBER;
    double max_width = -1;

    // Short loop to find widest dimension
    for (index_t d = 0; d < dim_; d++) {
      double w = node->bound().get(d).width();

      if (unlikely(w > max_width)) {
        max_width = w;
        split_dim = d;
      }
    }

    double split_val = node->bound().get(split_dim).mid();

    if (max_width != 0) {
      index_t left_i = allocator_.Alloc();
      index_t right_i = allocator_.Alloc();
      Node *left = nodes_.StartWrite(left_i);
      Node *right = nodes_.StartWrite(right_i);

      left->bound().Init(dim_);
      right->bound().Init(dim_);

      index_t split_col = Partition_(split_dim, split_val,
          node->begin(), node->count(),
          &left->bound(),
          &right->bound());
      
      DEBUG_MSG(3.0,"split (%d,[%d],%d) split_dim %d on %f (between %f, %f)",
          node->begin(), split_col,
          node->begin() + node->count(), split_dim, split_val,
          node->bound().get(split_dim).lo,
          node->bound().get(split_dim).hi);

      left->Init(node->begin(),
          split_col - node->begin());
      right->Init(split_col,
          node->end() - split_col);

      // This should never happen if max_width > 0
      DEBUG_ASSERT(left->count() != 0 && right->count() != 0);

      Build_(left_i);
      Build_(right_i);
      
      node->set_child(0, left_i);
      node->set_child(1, right_i);
      
      node->stat().Accumulate(*param_, left->stat(),
          left->bound(), left->count());
      node->stat().Accumulate(*param_, right->stat(),
          right->bound(), right->count());
      node->stat().Postprocess(*param_, node->bound(),
          node->count());
      
      leaf = false;
    }
  }

  if (leaf) {
    node->set_leaf();
  
    for (index_t i = node->begin(); i < node->end(); i++) {
      node->stat().Accumulate(*param_, points_[i], point_infos_[i]);
    }
    node->stat().Postprocess(*param_, node->bound(), node->count());
  }
  
  nodes_.StopWrite(node_i);
}

template<typename TPointInfo, typename TNode, typename TParam>
void KdTreeMidpointBuilder<TPointInfo, TNode, TParam>::Build() {
  index_t node_i = 0;

  nodes_.AddBack();
  node->Init(0, points_.size());
  node->bound().Init(dim_);
  
  FindBoundingBox_(node_i, points_.size(), &node->bound());

  Build_(0);
}

#endif
