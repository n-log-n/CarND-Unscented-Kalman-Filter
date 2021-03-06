#include "ukf.h"
#include "tools.h"
#include "Eigen/Dense"
#include <iostream>

using namespace std;
using Eigen::MatrixXd;
using Eigen::VectorXd;
using std::vector;

/**
 * Initializes Unscented Kalman filter
 */
UKF::UKF() {
  // if this is false, laser measurements will be ignored (except during init)
  use_laser_ = true;

  // if this is false, radar measurements will be ignored (except during init)
  use_radar_ = true;

  n_x_ = 5;

  n_aug_ = 7;

  // initial state vector
  x_ = VectorXd(n_x_);

  // initial covariance matrix
  P_ = MatrixXd(n_x_, n_x_);

  // Process noise standard deviation longitudinal acceleration in m/s^2
  std_a_ = 0.25;

  // Process noise standard deviation yaw acceleration in rad/s^2
  std_yawdd_ = 0.25;

  // Laser measurement noise standard deviation position1 in m
  std_laspx_ = 0.15;

  // Laser measurement noise standard deviation position2 in m
  std_laspy_ = 0.15;

  // Radar measurement noise standard deviation radius in m
  std_radr_ = 0.3;

  // Radar measurement noise standard deviation angle in rad
  std_radphi_ = 0.03;

  // Radar measurement noise standard deviation radius change in m/s
  std_radrd_ = 0.3;

  /**
  TODO:

  Complete the initialization. See ukf.h for other member properties.

  Hint: one or more values initialized above might be wildly off...
  */
  is_initialized_ = false;

  lambda_ = 3.0 - n_x_;

  weights_ = VectorXd(2 * n_aug_ + 1);
  weights_ = VectorXd::Ones(2 * n_aug_ + 1) * (0.5 /  (lambda_ + n_aug_));
  weights_(0) = lambda_ / (lambda_ + n_aug_);

  R_radar_ = MatrixXd(3, 3);
  R_radar_.fill(0.0);
  R_radar_(0, 0) = std_radr_ * std_radr_;
  R_radar_(1, 1) = std_radphi_ * std_radphi_;
  R_radar_(2, 2) = std_radrd_ * std_radrd_;

  R_lidar_ = MatrixXd(2, 2);
  R_lidar_.fill(0.0);
  R_lidar_(0, 0) = std_laspx_ * std_laspx_;
  R_lidar_(1, 1) = std_laspy_ * std_laspy_;

  Xsig_pred_ = MatrixXd(n_x_, 2 * n_aug_ + 1);

  NIS_radar_ = 0.0;
  NIS_laser_ = 0.0;
}

UKF::~UKF() {}

/**
 * @param {MeasurementPackage} meas_package The latest measurement data of
 * either radar or laser.
 */
void UKF::ProcessMeasurement(MeasurementPackage meas_package) {
  /**
  TODO:

  Complete this function! Make sure you switch between lidar and radar
  measurements.
  */

  if (! is_initialized_) {
    // Hanlding the first measurment here. 

    if (meas_package.sensor_type_ == MeasurementPackage::RADAR) {
      // initialize using the RADAR data.

      if (! this->use_radar_) return;

      double x = meas_package.raw_measurements_[0] * cos(meas_package.raw_measurements_[1]);
      double y = meas_package.raw_measurements_[0] * sin(meas_package.raw_measurements_[1]);

      x_ << x, y, 0, 0, 0;

    } else if (meas_package.sensor_type_ == MeasurementPackage::LASER) {
      
      if (! this->use_laser_) return;

      x_ << meas_package.raw_measurements_[0], meas_package.raw_measurements_[1], 0.0, 0.0, 0.0;

    }

    P_ = MatrixXd::Identity(n_x_, n_x_);
    time_us_ = meas_package.timestamp_;

    is_initialized_ = true;

    return;
  }

  float delta_t = (meas_package.timestamp_ - time_us_) / 1000000.0;
  time_us_ = meas_package.timestamp_;

  this->Prediction(delta_t);

  if (meas_package.sensor_type_ == MeasurementPackage::RADAR && this->use_radar_) {
    this->UpdateRadar(meas_package);
  } else if (meas_package.sensor_type_ == MeasurementPackage::LASER && this->use_laser_) {
    this->UpdateLidar(meas_package);
  }
}

/**
 * Predicts sigma points, the state, and the state covariance matrix.
 * @param {double} delta_t the change in time (in seconds) between the last
 * measurement and this one.
 */
void UKF::Prediction(double delta_t) {
  /**
  TODO:

  Complete this function! Estimate the object's location. Modify the state
  vector, x_. Predict sigma points, the state, and the state covariance matrix.
  */

  VectorXd x_aug = VectorXd(n_aug_);
  MatrixXd P_aug = MatrixXd(n_aug_, n_aug_);
  MatrixXd Xsig_aug = MatrixXd(n_aug_, 2 * n_aug_ + 1);

  // STEP 1 : Compute the sigma points

  x_aug.head(n_x_) = x_;
  x_aug(5) = 0.0;
  x_aug(6) = 0.0;

  //create augmented covariance matrix
  P_aug.fill(0.0);
  P_aug.topLeftCorner(n_x_, n_x_) = P_;
  P_aug(n_aug_ - 2, n_aug_ - 2) = std_a_ * std_a_;
  P_aug(n_aug_ - 1, n_aug_ - 1) = std_yawdd_ * std_yawdd_;
  //create square root matrix  
  MatrixXd A_aug = P_aug.llt().matrixL();
  
  //create augmented sigma points
  Xsig_aug.col(0) = x_aug;
  float lambda_naug_sqrt = sqrt(lambda_ + n_aug_);
  for (int i = 0; i < n_aug_; i++) {
    Xsig_aug.col(i + 1) = x_aug + lambda_naug_sqrt * A_aug.col(i);
    Xsig_aug.col(i + n_aug_ + 1) = x_aug - lambda_naug_sqrt * A_aug.col(i);
  }

  // STEP 2: Preeict the sigma points.
  for (int i = 0 ; i < Xsig_aug.cols(); i++) {
    double px = Xsig_aug(0, i);
    double py = Xsig_aug(1, i);
    double v = Xsig_aug(2, i);
    double phi = Xsig_aug(3, i);
    double phidot = Xsig_aug(4, i);
    double sigma_a = Xsig_aug(5, i);
    double sigma_phidotdot = Xsig_aug(6, i);
        
    double f_px = 0.0;
    double f_py = 0.0;
        
    if (fabs(phidot) <= 0.001) {
      f_px = px + v * cos(phi) * delta_t;
      f_py = py + v * sin(phi) * delta_t;
    } else {
      f_px = px + (v/phidot) * (sin(phi + phidot * delta_t) - sin(phi));
      f_py = py + (v/phidot) * (cos(phi) - cos(phi + phidot * delta_t));
    }
        
    Xsig_pred_.col(i) << f_px + 0.5 * delta_t * delta_t * cos(phi) * sigma_a,
                        f_py + 0.5 * delta_t * delta_t * sin(phi) * sigma_a,
                        v + (delta_t * sigma_a),
                        phi + (phidot * delta_t) + (0.5 * delta_t * delta_t * sigma_phidotdot),
                        phidot + delta_t * sigma_phidotdot;
  }

  // STEP 3: Complete the prediction step by computing the mean and co-varaince of sigma points. 
  x_.fill(0.0);
  for (int i = 0; i < 2 * n_aug_ + 1 ; i++) {
    x_ = x_ + weights_(i) * Xsig_pred_.col(i);
  }

  P_.fill(0.0);
  for (int i = 0 ; i < (2 * n_aug_ + 1); i++) {
    VectorXd diff = Xsig_pred_.col(i) - x_;

    while (diff(3)> M_PI) diff(3) -= 2.*M_PI;
    while (diff(3)<-M_PI) diff(3) += 2.*M_PI;

    P_ += weights_(i) * diff * diff.transpose();
  }

}

/**
 * Updates the state and the state covariance matrix using a laser measurement.
 * @param {MeasurementPackage} meas_package
 */
void UKF::UpdateLidar(MeasurementPackage meas_package) {
  /**
  TODO:

  Complete this function! Use lidar data to update the belief about the object's
  position. Modify the state vector, x_, and covariance, P_.

  You'll also need to calculate the lidar NIS.
  */

  MatrixXd Zsig = MatrixXd(2, 2 * n_aug_ + 1);
  MatrixXd S = MatrixXd(2, 2);
  VectorXd z_pred = VectorXd(2);


  for (int i=0; i < Xsig_pred_.cols(); i++) {
    double px = Xsig_pred_(0, i);
    double py = Xsig_pred_(1, i);
      
    VectorXd zk(2);
    zk << px, py; 
    Zsig.col(i) = zk;
  }

  z_pred.fill(0.0);
  for (int i = 0; i < 2 *n_aug_+1; i++) {
    z_pred = z_pred + weights_(i) * Zsig.col(i);
  }
  
  S.fill(0.0);
  for (int i = 0 ; i < Zsig.cols(); i++) {
    VectorXd diff = Zsig.col(i) - z_pred;
    S += diff * diff.transpose() * weights_(i);
  }
  
  S += R_lidar_;

  MatrixXd Tc = MatrixXd(n_x_, 2);
  Tc.fill(0.0);
  for (int i = 0 ; i < Xsig_pred_.cols(); i++) {
      VectorXd diff1 = Xsig_pred_.col(i) - x_;
      VectorXd diff2 = Zsig.col(i) - z_pred;
      Tc += weights_(i) * diff1 * diff2.transpose();
  }
  
  // Finally update the current state prediciton.
  VectorXd z_diff = meas_package.raw_measurements_ - z_pred; 
  MatrixXd K = Tc * S.inverse();
  x_ = x_ + K * z_diff;
  P_ = P_ - K * S * K.transpose();

  // Compute the NIS value. 

  this->NIS_laser_ = z_diff.transpose() * S.inverse() * z_diff;
}

/**
 * Updates the state and the state covariance matrix using a radar measurement.
 * @param {MeasurementPackage} meas_package
 */
void UKF::UpdateRadar(MeasurementPackage meas_package) {
  /**
  TODO:

  Complete this function! Use radar data to update the belief about the object's
  position. Modify the state vector, x_, and covariance, P_.

  You'll also need to calculate the radar NIS.
  */

  MatrixXd Zsig = MatrixXd(3, 2 * n_aug_ + 1);
  MatrixXd S = MatrixXd(3, 3);
  VectorXd z_pred = VectorXd(3);

  for (int i=0; i < 2 * n_aug_ + 1; i++) {
    double px = Xsig_pred_(0, i);
    double py = Xsig_pred_(1, i);
    double v = Xsig_pred_(2, i);
    double phi = Xsig_pred_(3, i);
      
    if (fabs(px) < 0.001) continue;
    if ( (px*px + py*py) < 0.001 ) continue;

    double v1 = cos(phi) * v;
    double v2 = sin(phi) * v;
      
    double p = sqrt(px*px + py*py);
    double phi_r = atan2(py, px);
    double pdot = (px * v1 + py * v2) / sqrt(px*px + py*py);
      
    VectorXd zk(3);
    zk << p, phi_r, pdot;

    Zsig.col(i) = zk;
  }

  z_pred.fill(0.0);
  for (int i = 0; i < 2 *n_aug_+1; i++) {
    z_pred = z_pred + weights_(i) * Zsig.col(i);
  }
  
  S.fill(0.0);
  for (int i = 0 ; i < 2 * n_aug_ + 1; i++) {
      VectorXd diff = Zsig.col(i) - z_pred;

      while (diff(1) > M_PI) diff(1) -= 2. * M_PI;
      while (diff(1) < -M_PI) diff(1) += 2.* M_PI;

      S =  S + weights_(i) * diff * diff.transpose();
  }
  
  S += R_radar_;

  MatrixXd Tc = MatrixXd(n_x_, 3);
  Tc.fill(0.0);
  for (int i = 0 ; i < Xsig_pred_.cols(); i++) {
      VectorXd diff1 = Xsig_pred_.col(i) - x_;

      while (diff1(1) > M_PI) diff1(1) -= 2. * M_PI;
      while (diff1(1) < -M_PI) diff1(1) += 2. * M_PI;

      VectorXd diff2 = Zsig.col(i) - z_pred;

      while (diff2(1) > M_PI) diff2(1) -= 2. * M_PI;
      while (diff2(1) < -M_PI) diff2(1) += 2. * M_PI;

      Tc += weights_(i) * diff1 * diff2.transpose();
  }
  
  // Finally update the current state prediciton. 
  VectorXd z_diff = meas_package.raw_measurements_ - z_pred;

  //angle normalization
  while (z_diff(1)> M_PI) z_diff(1)-=2.*M_PI;
  while (z_diff(1)<-M_PI) z_diff(1)+=2.*M_PI;

  MatrixXd K = Tc * S.inverse();
  x_ = x_ + K * z_diff;
  P_ = P_ - K * S * K.transpose();

  this->NIS_radar_ = z_diff.transpose() * S.inverse() * z_diff;

}
