/**
 * @author Parikshit Ram (pram@cc.gatech.edu)
 * @file mog_l2e.cpp
 *
 * Implementation for L2 loss function, and also some initial points generator.
 */
#include "mog_l2e.hpp"
#include "phi.hpp"
#include "kmeans.hpp"

using namespace mlpack;
using namespace gmm;

long double MoGL2E::L2Error(const arma::mat& data)
{
  return RegularizationTerm_() - (2 * GoodnessOfFitTerm_(data)) / data.n_colse;
}

long double MoGL2E::L2Error(const arma::mat& data, arma::vec& gradients)
{
  arma::vec g_reg, g_fit;

  long double l2e = RegularizationTerm_(g_reg) -
      (2 * GoodnessOfFitTerm_(data, g_fit)) / data.n_cols;

  gradients = g_reg - (2 * g_fit) / data.n_cols;

  return l2e;
}

long double MoGL2E::RegularizationTerm_()
{
  arma::mat phi_mu(gaussians, gaussians)

  // Fill the phi_mu matrix (which is symmetric).  Each entry of the matrix is
  // the phi() function evaluated on the difference between the means of each
  // Gaussian using the sum of the covariances of the two mixtures.
  for (size_t k = 1; k < gaussians; k++)
  {
    for (size_t j = 0; j < k; j++)
    {
      long double tmpVal = phi(means[k], means[j], covariances[k] +
          covariances[j]);

      phi_mu(j, k) = tmpVal;
      phi_mu(k, j) = tmpVal;
    }
  }

  // Because the difference between the means is 0, save a little time by only
  // doing the part of the calculation we need to (instead of calling phi()).
  for(size_t k = 0; k < gaussians; k++)
    phi_mu(k, k) = pow(2 * M_PI, (double) means[k].n_elem / -2.0)
        * pow(det(2 * covariances[k]), -0.5);

  return dot(weights, weights * phi_mu);
}

long double MoGL2E::RegularizationTerm_(arma::vec& g_reg)
{
  arma::mat phi_mu(gaussians, gaussians);
  arma::vec x, y;
  long double reg, tmpVal;

  arma::vec df_dw, g_omega;

  std::vector<arma::vec> g_mu(gaussians, arma::vec(dimension));
  std::vector<arma::vec> g_sigma(gaussians, arma::vec((dimension * (dimension
      + 1)) / 2));

  std::vector<std::vector<arma::vec> > dp_d_mu(gaussians,
      std::vector<arma::vec>(gaussians));
  std::vector<std::vector<arma::vec> > dp_d_sigma(gaussians,
      std::vector<arma::vec>(gaussians));

  x = weights;

  // Fill the phi_mu matrix (which is symmetric).  Each entry of the matrix is
  // the phi() function evaluated on the difference between the means of each
  // Gaussian using the sum of the covariances of the two mixtures.
  for(size_t k = 1; k < gaussians; k++)
  {
    for(size_t j = 0; j < k; j++)
    {
      std::vector<arma::mat> tmp_d_cov(dimension * (dimension + 1));
      arma::vec tmp_dp_d_sigma;

      // We should find a way to avoid all this copying to set up for the call
      // to phi().
      for(size_t i = 0; i < (dimension * (dimension + 1) / 2); i++)
      {
        tmp_d_cov[i] = (covariancesGradients[k])[i];
        tmp_d_cov[(dimension * (dimension + 1) / 2) + i] =
            (covariancesGradients[j])[i];
      }

      tmpVal = phi(means[k], means[j], covariances[k] + covariances[j],
          tmp_d_cov, dp_d_mu[j][k], tmp_dp_d_sigma);

      phi_mu(j, k) = tmpVal;
      phi_mu(k, j) = tmpVal;

      dp_d_mu[k][j] = -dp_d_mu[j][k];

      dp_d_sigma[j][k] = tmp_dp_d_sigma.rows(0,
          (dimension * (dimension + 1) / 2) - 1);
      dp_d_sigma[k][j] = tmp_dp_d_sigma.rows((dimension * (dimension + 1) / 2),
          tmp_dp_d_sigma.n_rows - 1);
    }
  }

  // Fill the diagonal elements of the phi_mu matrix.
  for (size_t k = 0; k < gaussians; k++)
  {
    arma::vec junk; // This result is not needed.
    phi_mu(k, k) = phi(means[k], means[k], 2 * covariances[k],
        covariancesGradients[k], junk, dp_d_sigma[k][k]);

    dp_d_mu[k][k].zeros(dimension);
  }

  // Calculate the regularization term value.
  arma::vec y = weights * phi_mu;
  long double reg = dot(weights, y);

  // Calculate the g_omega value; a vector of size K - 1
  df_dw = 2.0 * y;
  g_omega = weightsGradients * df_dw;

  // Calculate the g_mu values; K vectors of size D
  for (size_t k = 0; k < gaussians; k++)
  {
    for (size_t j = 0; j < gaussians; j++)
      g_mu[k] = 2.0 * weights[k] * weights[j] * dp_d_mu[j][k];

    // Calculating the g_sigma values - K vectors of size D(D+1)/2
    for (size_t k = 0; k < gaussians; k++)
    {
      for (size_t j = 0; j < gaussians; j++)
        g_sigma[k] += x[k] * dp_d_sigma[j][k];
      g_sigma[k] *= 2.0 * x[k];
    }

    // Making the single gradient vector of size K*(D+1)*(D+2)/2 - 1
    arma::vec tmp_g_reg((gaussians * (dimension + 1) *
        (dimension * 2) / 2) - 1);
    size_t j = 0;
    for (size_t k = 0; k < g_omega.n_elem; k++)
      tmp_g_reg[k] = g_omega[k];
    j = g_omega.n_elem;

    for (size_t k = 0; k < gaussians; k++) {
      for (size_t i = 0; i < dimension; i++)
        tmp_g_reg[j + (k * dimension) + i] = (g_mu[k])[i];

      for(size_t i = 0; i < (dimension * (dimension + 1) / 2); i++) {
        tmp_g_reg[j + (gaussians * dimension)
            + k * (dimension * (dimension + 1) / 2)
            + i] = (g_sigma[k])[i];
      }
    }

    g_reg = tmp_g_reg;
  }

  return reg;
}

long double MoGL2E::GoodnessOfFitTerm_(const arma::mat& data) {
  long double fit;
  arma::mat phi_x(gaussians, data.n_cols);
  arma::vec identity_vector;

  identity_vector.ones(data.n_cols);

  for (size_t k = 0; k < gaussians; k++)
    for (size_t i = 0; i < data.n_cols; i++)
      phi_x(k, i) = phi(data.unsafe_col(i), means[k], covariances[k]);

  fit = dot(weights * phi_x, identity_vector);

  return fit;
}

long double MoGL2E::GoodnessOfFitTerm_(const arma::mat& data,
                                       arma::vec& g_fit) {
  long double fit;
  arma::mat phi_x(gaussians, data.n_cols);
  arma::vec weights_l, x, y, identity_vector;
  arma::vec g_omega, tmp_g_omega;
  std::vector<arma::vec> g_mu, g_sigma;

  weights_l = weights;
  x.set_size(data.n_rows);
  identity_vector.ones(data.n_cols);

  g_mu.resize(gaussians);
  g_sigma.resize(gaussians);

  for(size_t k = 0; k < gaussians; k++) {
    g_mu[k].zeros(dimension);
    g_sigma[k].zeros(dimension * (dimension + 1) / 2);

    for (size_t i = 0; i < data.n_cols; i++) {
      arma::vec tmp_g_mu, tmp_g_sigma;
      phi_x(k, i) = phi(data.unsafe_col(i), means[k], covariances[k],
          d_sigma_[k], tmp_g_mu, tmp_g_sigma);

      g_mu[k] += tmp_g_mu;
      g_sigma[k] = tmp_g_sigma;
    }

    g_mu[k] *= weights_l[k];
    g_sigma[k] *= weights_l[k];
  }

  fit = dot(weights_l * phi_x, identity_vector);

  // Calculating the g_omega
  tmp_g_omega = phi_x * identity_vector;
  g_omega = d_omega_ * tmp_g_omega;

  // Making the single gradient vector of size K*(D+1)*(D+2)/2
  arma::vec tmp_g_fit((gaussians * (dimension + 1) *
      (dimension * 2) / 2) - 1);
  size_t j = 0;
  for (size_t k = 0; k < g_omega.n_elem; k++)
    tmp_g_fit[k] = g_omega[k];
  j = g_omega.n_elem;
  for (size_t k = 0; k < gaussians; k++) {
    for (size_t i = 0; i < dimension; i++)
      tmp_g_fit[j + (k * dimension) + i] = (g_mu[k])[i];

    for (size_t i = 0; i < (dimension * (dimension + 1) / 2); i++)
      tmp_g_fit[j + gaussians * dimension
        + k * (dimension * (dimension + 1) / 2) + i] = (g_sigma[k])[i];
  }

  g_fit = tmp_g_fit;

  return fit;
}

void MoGL2E::MultiplePointsGenerator(arma::mat& points,
                                     const arma::mat& d,
                                     size_t number_of_components) {

  size_t i, j, x;

  for (i = 0; i < points.n_rows; i++)
    for (j = 0; j < points.n_cols - 1; j++)
      points(i, j) = (rand() % 20001) / 1000 - 10;

  for (i = 0; i < points.n_rows; i++) {
    for (j = 0; j < points.n_cols; j++) {
      arma::vec tmp_mu = d.col(rand() % d.n_cols);
      for (x = 0; x < d.n_rows; x++)
        points(i, number_of_components - 1 + (j * d.n_rows) + x) = tmp_mu[x];
    }
  }

  for (i = 0; i < points.n_rows; i++)
    for (j = 0; j < points.n_cols; j++)
      for (x = 0; x < (d.n_rows * (d.n_rows + 1) / 2); x++)
        points(i, (number_of_components * (d.n_rows + 1) - 1)
          + (j * (d.n_rows * (d.n_rows + 1) / 2)) + x) = (rand() % 501) / 100;

  return;
}

void MoGL2E::InitialPointGenerator(arma::vec& theta,
                                   const arma::mat& data,
                                   size_t k_comp) {
  std::vector<arma::vec> means_l;
  std::vector<arma::mat> covars;
  arma::vec weights_l;
  double noise;

  weights_l.set_size(k_comp);
  means_l.resize(k_comp);
  covars.resize(k_comp);

  theta.set_size(k_comp);

  for (size_t i = 0; i < k_comp; i++) {
    means_l[i].set_size(data.n_rows);
    covars[i].set_size(data.n_rows, data.n_rows);
  }

  KMeans(data, k_comp, means_l, covars, weights_l);

  for (size_t k = 0; k < k_comp - 1; k++) {
    noise = (double) (rand() % 10000) / (double) 1000;
    theta[k] = noise - 5;
  }

  for (size_t k = 0; k < k_comp; k++) {
    for (size_t j = 0; j < data.n_rows; j++)
      theta[k_comp - 1 + k * data.n_rows + j] = (means_l[k])[j];

    arma::mat u = chol(covars[k]);
    for(size_t j = 0; j < data.n_rows; j++)
      for(size_t i = 0; i < j + 1; i++)
        theta[k_comp - 1 + (k_comp * data.n_rows)
            + (k * data.n_rows * (data.n_rows + 1) / 2)
            + (j * (j + 1) / 2 + i)] = u(i, j) + ((rand() % 501) / 100);
  }
}
