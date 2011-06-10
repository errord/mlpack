/** @file fgt_kde_main.cc
 *
 *  Driver for the fast Gauss transform based KDE algorithm.
 *
 *  @author Dongryeol Lee (dongryel)
 */

#include <fastlib/fastlib.h>
#include <fastlib/fx/io.h>

#include "dataset_scaler.h"
#include "fgt_kde.h"
#include "naive_kde.h"

#include <armadillo>
#include <fastlib/base/arma_compat.h>

/**
 * Main function which reads parameters and determines which
 * algorithms to run.
 *
 * In order to compile this driver, do:
 * fl-build fgt_kde_bin --mode=fast
 *
 * In order to run this driver for the FGT-based KDE algorithm, type
 * the following (which consists of both required and optional
 * arguments) in a single command line:
 *
 * ./fgt_kde_bin --data=name_of_the_reference_dataset
 *               --query=name_of_the_query_dataset
 *               --kde/bandwidth=0.0130619
 *               --kde/scaling=range
 *               --kde/fgt_kde_output=fgt_kde_output.txt
 *               --kde/naive_kde_output=naive_kde_output.txt
 *               --kde/do_naive
 *               --kde/absolute_error=0.1
 *
 * Explanations for the arguments listed with possible values:
 *
 * 1. data (required): the name of the reference dataset
 *
 * 2. query (optional): the name of the query dataset (if missing, the
 * query dataset is assumed to be the same as the reference dataset)
 *
 * 3. kde/bandwidth (required): smoothing parameter used for KDE; this
 * has to be positive.
 *
 * 4. kde/scaling (optional): whether to prescale the dataset - range:
 * scales both the query and the reference sets to be within the unit
 * hypercube [0, 1]^D where D is the dimensionality.  - none: default
 * value; no scaling
 *
 * 5. kde/do_naive (optional): run the naive algorithm after the fast
 * algorithm.
 *
 * 6. kde/fgt_kde_output (optional): if this flag is present, the
 * approximated density estimates are output to the filename provided
 * after it.
 *
 * 7. kde/naive_kde_output (optional): if this flag is present, the
 * exact density estimates computed by the naive algorithm are output
 * to the filename provided after it. This flag is not ignored if
 * --kde/do_naive flag is not present.
 * 
 * 8. kde/absolute_error (optional): absolute error criterion for the
 * fast algorithm; default value is 0.1 (0.1 absolute error for all
 * query density estimates).
 */

using namespace mlpack;

int main(int argc, char *argv[]) {

  // initialize FastExec (parameter handling stuff)
  fx_init(argc, argv, NULL);
  
  ////////// READING PARAMETERS AND LOADING DATA /////////////////////

  // FASTexec organizes parameters and results into submodules.  Think
  // of this as creating a new folder named "fgt_kde_module" under the
  // root directory (NULL) for the Kde object to work inside.  Here,
  // we initialize it with all parameters defined "--kde/...=...".

  // The reference data file is a required parameter.
  const char* references_file_name = IO::GetParam<std::string>("kde/data").c_str();

  // The query data file defaults to the references.
  const char* queries_file_name;
  if(!IO::HasParam("kde/query"))
    queries_file_name = references_file_name;  
  else
    queries_file_name = IO::GetParam<std::string>("kde/query").c_str();

  // query and reference datasets
  arma::mat references;
  arma::mat* queries_ptr; // I don't like doing this like this
  arma::mat queries;

  // flag for telling whether references are equal to queries
  bool queries_equal_references =
    !strcmp(queries_file_name, references_file_name);

  // data::Load inits a matrix with the contents of a .csv or .arff.
  data::Load(references_file_name, references);
  if(queries_equal_references) {
    queries_ptr = &references;
  }
  else {
    data::Load(queries_file_name, queries);
    queries_ptr = &queries;
  }

  // confirm whether the user asked for scaling of the dataset
  if(!strcmp(IO::GetParam<std::string>("kde/scaling").c_str(), "range")) {
    DatasetScaler::ScaleDataByMinMax(*queries_ptr, references,
                                     queries_equal_references);
  }

  // declare FGT-based KDE computation object and the vector holding
  // the final results
  FGTKde fgt_kde;
  arma::vec fgt_kde_results;

  fgt_kde.Init(*queries_ptr, references);
  fgt_kde.Compute();
  fgt_kde.get_density_estimates(fgt_kde_results);

  // print out the results if the user specified the flag for output
  if(IO::HasParam("kde/fgt_kde_output")) {
    fgt_kde.PrintDebug();
  }

  // do naive computation and compare to the FGT computations if the
  // user specified --do_naive flag
  if(IO::HasParam("kde/do_naive")) {
    NaiveKde<GaussianKernel> naive_kde;
    naive_kde.Init(*queries_ptr, references);
    naive_kde.Compute();
    
    if(IO::HasParam("kde/naive_kde_output")) {
      naive_kde.PrintDebug();
    }
    naive_kde.ComputeMaximumRelativeError(fgt_kde_results);
  }
  return 0;
}
