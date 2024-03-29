// Copyright 2020 PAL Robotics S.L.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*
 * Author: Bence Magyar, Enrique Fernández, Manuel Meraz
 * 
 * Maintainer: Faiz Pangestu
 */

#define _USE_MATH_DEFINES
#include <cmath>

#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "ack_6wd_controller/ack_6wd_controller.hpp"
#include "hardware_interface/types/hardware_interface_type_values.hpp"
#include "lifecycle_msgs/msg/state.hpp"
#include "rclcpp/logging.hpp"
#include "tf2/LinearMath/Quaternion.h"

namespace
{
constexpr auto DEFAULT_COMMAND_TOPIC = "/cmd_vel";
constexpr auto DEFAULT_COMMAND_UNSTAMPED_TOPIC = "/cmd_vel";
constexpr auto DEFAULT_COMMAND_OUT_TOPIC = "~/cmd_vel_out";
constexpr auto DEFAULT_ODOMETRY_TOPIC = "/odom";
constexpr auto DEFAULT_TRANSFORM_TOPIC = "/tf";
}  // namespace

namespace ack_6wd_controller
{
using namespace std::chrono_literals;
using controller_interface::interface_configuration_type;
using controller_interface::InterfaceConfiguration;
using hardware_interface::HW_IF_POSITION;
using hardware_interface::HW_IF_VELOCITY;
using lifecycle_msgs::msg::State;

Ack6WDController::Ack6WDController() : controller_interface::ControllerInterface() {}

controller_interface::return_type Ack6WDController::init(const std::string & controller_name)
{
  // initialize lifecycle node
  auto ret = ControllerInterface::init(controller_name);
  if (ret != controller_interface::return_type::OK)
  {
    return ret;
  }

  try
  {
    // with the lifecycle node being initialized, we can declare parameters
    auto_declare<std::vector<std::string>>("left_wheel_names", std::vector<std::string>());
    auto_declare<std::vector<std::string>>("right_wheel_names", std::vector<std::string>());
    auto_declare<std::vector<std::string>>("left_steering_names", std::vector<std::string>());
    auto_declare<std::vector<std::string>>("right_steering_names", std::vector<std::string>());

    auto_declare<std::vector<std::string>>("middle_wheel_names", std::vector<std::string>());

    auto_declare<double>("wheel_base", wheel_params_.base);
    auto_declare<double>("wheel_separation", wheel_params_.separation);
    auto_declare<int>("wheels_per_side", wheel_params_.wheels_per_side);
    auto_declare<double>("wheel_radius", wheel_params_.radius);
    auto_declare<double>("wheel_base_multiplier", wheel_params_.base_multiplier);
    auto_declare<double>("wheel_separation_multiplier", wheel_params_.separation_multiplier);
    auto_declare<double>("left_wheel_radius_multiplier", wheel_params_.left_radius_multiplier);
    auto_declare<double>("right_wheel_radius_multiplier", wheel_params_.right_radius_multiplier);
    auto_declare<double>("angular_velocity_compensation", wheel_params_.angular_velocity_compensation);
    auto_declare<double>("steering_angle_correction", wheel_params_.steering_angle_correction);


    auto_declare<std::string>("odom_frame_id", odom_params_.odom_frame_id);
    auto_declare<std::string>("base_frame_id", odom_params_.base_frame_id);
    auto_declare<std::vector<double>>("pose_covariance_diagonal", std::vector<double>());
    auto_declare<std::vector<double>>("twist_covariance_diagonal", std::vector<double>());
    auto_declare<bool>("open_loop", odom_params_.open_loop);
    auto_declare<bool>("enable_odom_tf", odom_params_.enable_odom_tf);

    auto_declare<double>("cmd_vel_timeout", cmd_vel_timeout_.count() / 1000.0);
    auto_declare<bool>("publish_limited_velocity", publish_limited_velocity_);
    auto_declare<int>("velocity_rolling_window_size", 10);
    auto_declare<bool>("use_stamped_vel", use_stamped_vel_);

    auto_declare<bool>("linear.x.has_velocity_limits", false);
    auto_declare<bool>("linear.x.has_acceleration_limits", false);
    auto_declare<bool>("linear.x.has_jerk_limits", false);
    auto_declare<double>("linear.x.max_velocity", NAN);
    auto_declare<double>("linear.x.min_velocity", NAN);
    auto_declare<double>("linear.x.max_acceleration", NAN);
    auto_declare<double>("linear.x.min_acceleration", NAN);
    auto_declare<double>("linear.x.max_jerk", NAN);
    auto_declare<double>("linear.x.min_jerk", NAN);

    auto_declare<bool>("angular.z.has_velocity_limits", false);
    auto_declare<bool>("angular.z.has_acceleration_limits", false);
    auto_declare<bool>("angular.z.has_jerk_limits", false);
    auto_declare<double>("angular.z.max_velocity", NAN);
    auto_declare<double>("angular.z.min_velocity", NAN);
    auto_declare<double>("angular.z.max_acceleration", NAN);
    auto_declare<double>("angular.z.min_acceleration", NAN);
    auto_declare<double>("angular.z.max_jerk", NAN);
    auto_declare<double>("angular.z.min_jerk", NAN);
    auto_declare<double>("publish_rate", publish_rate_);
  }
  catch (const std::exception & e)
  {
    fprintf(stderr, "Exception thrown during init stage with message: %s \n", e.what());
    return controller_interface::return_type::ERROR;
  }

  return controller_interface::return_type::OK;
}

InterfaceConfiguration Ack6WDController::command_interface_configuration() const
{
  std::vector<std::string> conf_names;
  for (const auto & joint_name : left_wheel_names_)
  {
    conf_names.push_back(joint_name + "/" + HW_IF_VELOCITY);
  }
  for (const auto & joint_name : right_wheel_names_)
  {
    conf_names.push_back(joint_name + "/" + HW_IF_VELOCITY);
  }
  for (const auto & joint_name : middle_wheel_names_)
  {
    conf_names.push_back(joint_name + "/" + HW_IF_VELOCITY);
  }
  for (const auto & joint_name : left_steering_names_)
  {
    conf_names.push_back(joint_name + "/" + HW_IF_POSITION);
  }
  for (const auto & joint_name : right_steering_names_)
  {
    conf_names.push_back(joint_name + "/" + HW_IF_POSITION);
  }
  return {interface_configuration_type::INDIVIDUAL, conf_names};
}

InterfaceConfiguration Ack6WDController::state_interface_configuration() const
{
  std::vector<std::string> conf_names;
  for (const auto & joint_name : left_wheel_names_)
  {
    conf_names.push_back(joint_name + "/" + HW_IF_POSITION);        
    conf_names.push_back(joint_name + "/" + HW_IF_VELOCITY);
  }
  for (const auto & joint_name : right_wheel_names_)
  {
    conf_names.push_back(joint_name + "/" + HW_IF_POSITION);
    conf_names.push_back(joint_name + "/" + HW_IF_VELOCITY);
  }
  for (const auto & joint_name : middle_wheel_names_)
  {
    conf_names.push_back(joint_name + "/" + HW_IF_POSITION);
    conf_names.push_back(joint_name + "/" + HW_IF_VELOCITY);
  }
  for (const auto & joint_name : left_steering_names_)
  {
    conf_names.push_back(joint_name + "/" + HW_IF_POSITION);
    conf_names.push_back(joint_name + "/" + HW_IF_VELOCITY);
  }
  for (const auto & joint_name : right_steering_names_)
  {
    conf_names.push_back(joint_name + "/" + HW_IF_POSITION);
    conf_names.push_back(joint_name + "/" + HW_IF_VELOCITY);
  }
  return {interface_configuration_type::INDIVIDUAL, conf_names};
}

controller_interface::return_type Ack6WDController::update()
{
  auto logger = node_->get_logger();
  
  if (get_current_state().id() == State::PRIMARY_STATE_INACTIVE)
  {
    if (!is_halted)
    {
      halt();
      is_halted = true;
    }
    return controller_interface::return_type::OK;
  }

  const auto current_time = node_->get_clock()->now();

  std::shared_ptr<Twist> last_msg;
  received_velocity_msg_ptr_.get(last_msg);

  if (last_msg == nullptr)
  {
    RCLCPP_WARN(logger, "Velocity message received was a nullptr.");
    return controller_interface::return_type::ERROR;
  }

  const auto dt = current_time - last_msg->header.stamp;
  // Brake if cmd_vel has timeout, override the stored command
  if (dt > cmd_vel_timeout_)
  {
    last_msg->twist.linear.x = 0.0;
    last_msg->twist.angular.z = 0.0;
  }

  // command may be limited further by SpeedLimit,
  // without affecting the stored twist command
  Twist command = *last_msg;
  double & linear_command = command.twist.linear.x;
  double & angular_command = command.twist.angular.z;

  // Apply (possibly new) multipliers:
  const auto wheels = wheel_params_;
  const double wheel_base = wheels.base_multiplier * wheels.base;
  const double wheel_separation = wheels.separation_multiplier * wheels.separation;
  const double left_wheel_radius = wheels.left_radius_multiplier * wheels.radius;
  const double right_wheel_radius = wheels.right_radius_multiplier * wheels.radius;
  const double ang_vel_comp = wheels.angular_velocity_compensation;
  const double steering_correction = wheels.steering_angle_correction;

  // Speed limiter
  if (angular_command != 0 && linear_command == 0){
    RCLCPP_ERROR(logger, "Turning radius is too short!\n");
    return controller_interface::return_type::ERROR;
  }

  if (odom_params_.open_loop)
  {
    odometry_.updateOpenLoop(linear_command, angular_command, current_time);
  }
  else
  {
    // double left_position_mean = 0.0;
    // double right_position_mean = 0.0;
    // for (size_t index = 0; index < wheels.wheels_per_side; ++index)
    // {
    //   const double left_position = registered_left_wheel_handles_[index].position.get().get_value();
    //   const double right_position =
    //     registered_right_wheel_handles_[index].position.get().get_value();

    //   if (std::isnan(left_position) || std::isnan(right_position))
    //   {
    //     RCLCPP_ERROR(
    //       logger, "Either the left or right wheel position is invalid for index [%zu]", index);
    //     return controller_interface::return_type::ERROR;
    //   }

    //   left_position_mean += left_position;
    //   right_position_mean += right_position;
    // }

    double left_velocity_mean = 0.0;
    double right_velocity_mean = 0.0;
    double left_angle_mean = 0.0;
    double right_angle_mean = 0.0;
    int q = 0;
    for (size_t index = 0; index < wheels.wheels_per_side; ++index)
    {
      const double left_velocity = registered_left_wheel_handles_[index].velocity.get().get_value() * 2 * 3.14 / 60; // to rad/s
      const double right_velocity = registered_right_wheel_handles_[index].velocity.get().get_value() * 2 * 3.14 / 60;
      const double left_angle = registered_left_steering_handles_[index].position.get().get_value();
      const double right_angle = registered_right_steering_handles_[index].position.get().get_value();

      if (index == 0){
        q = quadrant(left_velocity, left_angle);
      }

      if (std::isnan(left_velocity) || std::isnan(right_velocity))
      {
        RCLCPP_ERROR(
          logger, "Either the left or right wheel velocity is invalid for index [%zu]", index);
        return controller_interface::return_type::ERROR;
      }

      if (std::isnan(left_angle) || std::isnan(right_angle))
      {
        RCLCPP_ERROR(
          logger, "Either the left or right steering angle is invalid for index [%zu]", index);
        return controller_interface::return_type::ERROR;
      }

      left_velocity_mean += abs(left_velocity);
      right_velocity_mean += abs(right_velocity);

      left_angle_mean += abs(left_angle);
      right_angle_mean += abs(right_angle);
    }

    left_velocity_mean = left_velocity_mean/wheels.wheels_per_side;
    right_velocity_mean = right_velocity_mean/wheels.wheels_per_side;
    left_angle_mean = left_angle_mean/wheels.wheels_per_side;
    right_angle_mean = right_angle_mean/wheels.wheels_per_side;

    double velocity_encoder = std::min(std::abs(left_velocity_mean), std::abs(right_velocity_mean)) * (q == 0 || q == 1 ? 1 : -1);
    double angle_encoder = std::max(std::abs(left_angle_mean), std::abs(right_angle_mean)) * (q == 0 || q == 2 ? 1 : -1);

    // Debug mean
    // RCLCPP_INFO(logger, "Velocity: %f, Angle: %f",  velocity_encoder, angle_encoder);

    // odometry_.update(left_position_mean, right_position_mean, current_time);
    // RCLCPP_INFO(logger, "Velocity: %f, Angle: %f",  velocity_encoder, angle_encoder);
    odometry_.updateVel(angle_encoder, velocity_encoder, current_time);

    // Debug odom
    RCLCPP_INFO(logger, "DEBUG: %f", steering_correction);

  }

  tf2::Quaternion orientation;
  orientation.setRPY(0.0, 0.0, odometry_.getHeading());

  if (previous_publish_timestamp_ + publish_period_ < current_time)
  {
    previous_publish_timestamp_ += publish_period_;

    if (realtime_odometry_publisher_->trylock())
    {
      auto & odometry_message = realtime_odometry_publisher_->msg_;
      odometry_message.header.stamp = current_time;
      odometry_message.pose.pose.position.x = odometry_.getX();
      odometry_message.pose.pose.position.y = odometry_.getY();
      odometry_message.pose.pose.orientation.x = orientation.x();
      odometry_message.pose.pose.orientation.y = orientation.y();
      odometry_message.pose.pose.orientation.z = orientation.z();
      odometry_message.pose.pose.orientation.w = orientation.w();
      odometry_message.twist.twist.linear.x = odometry_.getLinear();
      odometry_message.twist.twist.angular.z = odometry_.getAngular();
      realtime_odometry_publisher_->unlockAndPublish();
    }

    if (odom_params_.enable_odom_tf && realtime_odometry_transform_publisher_->trylock())
    {
      auto & transform = realtime_odometry_transform_publisher_->msg_.transforms.front();
      transform.header.stamp = current_time;
      transform.transform.translation.x = odometry_.getX();
      transform.transform.translation.y = odometry_.getY();
      transform.transform.rotation.x = orientation.x();
      transform.transform.rotation.y = orientation.y();
      transform.transform.rotation.z = orientation.z();
      transform.transform.rotation.w = orientation.w();
      realtime_odometry_transform_publisher_->unlockAndPublish();
    }
  }

  const auto update_dt = current_time - previous_update_timestamp_;
  previous_update_timestamp_ = current_time;

  auto & last_command = previous_commands_.back().twist;
  auto & second_to_last_command = previous_commands_.front().twist;
  limiter_linear_.limit(
    linear_command, last_command.linear.x, second_to_last_command.linear.x, update_dt.seconds());
  limiter_angular_.limit(
    angular_command, last_command.angular.z, second_to_last_command.angular.z, update_dt.seconds());

  previous_commands_.pop();
  previous_commands_.emplace(command);

  //    Publish limited velocity
  if (publish_limited_velocity_ && realtime_limited_velocity_publisher_->trylock())
  {
    auto & limited_velocity_command = realtime_limited_velocity_publisher_->msg_;
    limited_velocity_command.header.stamp = current_time;
    limited_velocity_command.twist = command.twist;
    realtime_limited_velocity_publisher_->unlockAndPublish();
  }

  double angle_left, angle_right, velocity_left, velocity_right, turning_radius = -1;
  double velocity_mid_left, velocity_mid_right;

  if (angular_command == 0){
    velocity_left = abs(linear_command / left_wheel_radius);
    velocity_right = abs(linear_command / right_wheel_radius);
    velocity_mid_left = velocity_left;
    velocity_mid_right = velocity_right;

    angle_left = 0;
    angle_right = 0;
  } else if (linear_command != 0) {
    // Turning radius
    turning_radius = abs(linear_command / angular_command);

    // Compute steering angles: (pi = M_PI = 3.14........)
    angle_left = M_PI/2 - atan((2*turning_radius - wheel_base) / wheel_separation);
    angle_right = M_PI/2 - atan((2*turning_radius + wheel_base) / wheel_separation);

    // Axis distance
    const double left_axis = abs(wheel_separation / (2 * sin(angle_left)));
    const double right_axis = abs(wheel_separation / (2 * sin(angle_right)));

    // Compute wheels velocities:
    velocity_left = abs(angular_command * left_axis / left_wheel_radius) * ang_vel_comp;
    velocity_right = abs(angular_command * right_axis / right_wheel_radius) * ang_vel_comp;

    velocity_mid_left = abs(angular_command * (turning_radius - wheel_base) / left_wheel_radius) * ang_vel_comp;
    velocity_mid_right = abs(angular_command * (turning_radius + wheel_base) / right_wheel_radius) * ang_vel_comp;
  } else {
    RCLCPP_ERROR(logger, "Turning radius is too short!\n");
    return controller_interface::return_type::ERROR;
  }

  // Direction matrix
  int d[4][4] = {  
   {1, 1, 1, 1} ,   // Linear > 0, Angular > 0
   {-1, -1, 1, 1} , // Linear > 0, Angular < 0
   {-1, -1, -1, -1} , // Linear < 0, Angular > 0
   {1, 1, -1, -1} // Linear < 0, Angular < 0
  };

  int q = quadrant(linear_command, angular_command);
  // Quadrant
  // 0 | 1
  // -----
  // 3 | 2

  const double steering_angle_left = d[q][0] * (q == 0 || q == 3 ? angle_left : angle_right) * steering_correction;
  const double steering_angle_right = d[q][1] * (q == 0 || q == 3 ? angle_right : angle_left) * steering_correction;
  const double wheel_velocity_left = d[q][2] * (q == 0 || q == 3 ? velocity_left : velocity_right);
  const double wheel_velocity_right = d[q][3] * (q == 0 || q == 3 ? velocity_right : velocity_left);

  const double wheel_velocity_mid_left = d[q][2] * (q == 0 || q == 3 ? velocity_mid_left : velocity_mid_right);
  const double wheel_velocity_mid_right = d[q][3] * (q == 0 || q == 3 ? velocity_mid_right : velocity_mid_left);

  // Debugger
  // RCLCPP_INFO(logger, "velocity left, front: %f, steering: %f \nvelocity right, front: %f, steering: %f \n", 
  //             wheel_velocity_left * 60 / 6.283, 
  //             steering_angle_left, 
  //             wheel_velocity_right  * 60 / 6.283, 
  //             steering_angle_right);
  // RCLCPP_INFO(logger, "mid left %f, right: %f\n", 
  //             wheel_velocity_mid_left * 60 / 6.283,
  //             wheel_velocity_mid_right * 60 / 6.283);

  // Set motor state: set value type const double
  for (size_t index = 0; index < wheels.wheels_per_side; ++index)
  {
    registered_left_wheel_handles_[index].velocity.get().set_value(wheel_velocity_left * 60 / 6.283);  // to rpm
    registered_right_wheel_handles_[index].velocity.get().set_value(wheel_velocity_right * 60 / 6.283);
  }

  registered_middle_wheel_handles_[0].velocity.get().set_value(wheel_velocity_mid_right * 60 / 6.283); // Middle-right wheel
  registered_middle_wheel_handles_[1].velocity.get().set_value(wheel_velocity_mid_left * 60 / 6.283);  // Middle-left wheel

  registered_left_steering_handles_[0].position.get().set_value(steering_angle_left);     // Front wheels [rad]
  registered_right_steering_handles_[0].position.get().set_value(-steering_angle_right);

  registered_left_steering_handles_[1].position.get().set_value(-steering_angle_left);    // Rear wheels
  registered_right_steering_handles_[1].position.get().set_value(steering_angle_right);

  return controller_interface::return_type::OK;
}

CallbackReturn Ack6WDController::on_configure(const rclcpp_lifecycle::State &)
{
  auto logger = node_->get_logger();

  // update parameters for wheels
  left_wheel_names_ = node_->get_parameter("left_wheel_names").as_string_array();
  right_wheel_names_ = node_->get_parameter("right_wheel_names").as_string_array();

  middle_wheel_names_ = node_->get_parameter("middle_wheel_names").as_string_array();

  if (left_wheel_names_.size() != right_wheel_names_.size())
  {
    RCLCPP_ERROR(
      logger, "The number of left wheels [%zu] and the number of right wheels [%zu] are different",
      left_wheel_names_.size(), right_wheel_names_.size());
    return CallbackReturn::ERROR;
  }

  if (left_wheel_names_.empty())
  {
    RCLCPP_ERROR(logger, "Wheel names parameters are empty!");
    return CallbackReturn::ERROR;
  }

  if (middle_wheel_names_.empty())
  {
    RCLCPP_ERROR(logger, "Middle wheel names parameters are empty!");
    return CallbackReturn::ERROR;
  }

  // update parameters for steerings
  left_steering_names_ = node_->get_parameter("left_steering_names").as_string_array();
  right_steering_names_ = node_->get_parameter("right_steering_names").as_string_array();

  if (left_steering_names_.size() != right_steering_names_.size())
  {
    RCLCPP_ERROR(
      logger, "The number of left steerings [%zu] and the number of right steerings [%zu] are different",
      left_steering_names_.size(), right_steering_names_.size());
    return CallbackReturn::ERROR;
  }

  if (left_steering_names_.empty())
  {
    RCLCPP_ERROR(logger, "Wheel names parameters are empty!");
    return CallbackReturn::ERROR;
  }

  // update wheel params
  wheel_params_.base = node_->get_parameter("wheel_base").as_double();
  wheel_params_.separation = node_->get_parameter("wheel_separation").as_double();
  wheel_params_.wheels_per_side =
    static_cast<size_t>(node_->get_parameter("wheels_per_side").as_int());
  wheel_params_.radius = node_->get_parameter("wheel_radius").as_double();
  wheel_params_.base_multiplier =
    node_->get_parameter("wheel_base_multiplier").as_double();
  wheel_params_.separation_multiplier =
    node_->get_parameter("wheel_separation_multiplier").as_double();
  wheel_params_.left_radius_multiplier =
    node_->get_parameter("left_wheel_radius_multiplier").as_double();
  wheel_params_.right_radius_multiplier =
    node_->get_parameter("right_wheel_radius_multiplier").as_double();
  wheel_params_.angular_velocity_compensation =
    node_->get_parameter("angular_velocity_compensation").as_double();
  wheel_params_.steering_angle_correction =
    node_->get_parameter("steering_angle_correction").as_double();

  const auto wheels = wheel_params_;

  const double wheel_separation = wheels.separation_multiplier * wheels.separation;
  const double wheel_base = wheels.base_multiplier * wheels.base;
  const double left_wheel_radius = wheels.left_radius_multiplier * wheels.radius;
  const double right_wheel_radius = wheels.right_radius_multiplier * wheels.radius;

  odometry_.setWheelParams(wheel_separation, wheel_base, left_wheel_radius, right_wheel_radius);
  odometry_.setVelocityRollingWindowSize(
    node_->get_parameter("velocity_rolling_window_size").as_int());

  odom_params_.odom_frame_id = node_->get_parameter("odom_frame_id").as_string();
  odom_params_.base_frame_id = node_->get_parameter("base_frame_id").as_string();

  auto pose_diagonal = node_->get_parameter("pose_covariance_diagonal").as_double_array();
  std::copy(
    pose_diagonal.begin(), pose_diagonal.end(), odom_params_.pose_covariance_diagonal.begin());

  auto twist_diagonal = node_->get_parameter("twist_covariance_diagonal").as_double_array();
  std::copy(
    twist_diagonal.begin(), twist_diagonal.end(), odom_params_.twist_covariance_diagonal.begin());

  odom_params_.open_loop = node_->get_parameter("open_loop").as_bool();
  odom_params_.enable_odom_tf = node_->get_parameter("enable_odom_tf").as_bool();

  cmd_vel_timeout_ = std::chrono::milliseconds{
    static_cast<int>(node_->get_parameter("cmd_vel_timeout").as_double() * 1000.0)};
  publish_limited_velocity_ = node_->get_parameter("publish_limited_velocity").as_bool();
  use_stamped_vel_ = node_->get_parameter("use_stamped_vel").as_bool();

  try
  {
    limiter_linear_ = SpeedLimiter(
      node_->get_parameter("linear.x.has_velocity_limits").as_bool(),
      node_->get_parameter("linear.x.has_acceleration_limits").as_bool(),
      node_->get_parameter("linear.x.has_jerk_limits").as_bool(),
      node_->get_parameter("linear.x.min_velocity").as_double(),
      node_->get_parameter("linear.x.max_velocity").as_double(),
      node_->get_parameter("linear.x.min_acceleration").as_double(),
      node_->get_parameter("linear.x.max_acceleration").as_double(),
      node_->get_parameter("linear.x.min_jerk").as_double(),
      node_->get_parameter("linear.x.max_jerk").as_double());
  }
  catch (const std::runtime_error & e)
  {
    RCLCPP_ERROR(node_->get_logger(), "Error configuring linear speed limiter: %s", e.what());
  }

  try
  {
    limiter_angular_ = SpeedLimiter(
      node_->get_parameter("angular.z.has_velocity_limits").as_bool(),
      node_->get_parameter("angular.z.has_acceleration_limits").as_bool(),
      node_->get_parameter("angular.z.has_jerk_limits").as_bool(),
      node_->get_parameter("angular.z.min_velocity").as_double(),
      node_->get_parameter("angular.z.max_velocity").as_double(),
      node_->get_parameter("angular.z.min_acceleration").as_double(),
      node_->get_parameter("angular.z.max_acceleration").as_double(),
      node_->get_parameter("angular.z.min_jerk").as_double(),
      node_->get_parameter("angular.z.max_jerk").as_double());
  }
  catch (const std::runtime_error & e)
  {
    RCLCPP_ERROR(node_->get_logger(), "Error configuring angular speed limiter: %s", e.what());
  }

  if (!reset())
  {
    return CallbackReturn::ERROR;
  }

  // left and right sides are both equal at this point
  wheel_params_.wheels_per_side = left_wheel_names_.size();

  if (publish_limited_velocity_)
  {
    limited_velocity_publisher_ =
      node_->create_publisher<Twist>(DEFAULT_COMMAND_OUT_TOPIC, rclcpp::SystemDefaultsQoS());
    realtime_limited_velocity_publisher_ =
      std::make_shared<realtime_tools::RealtimePublisher<Twist>>(limited_velocity_publisher_);
  }

  const Twist empty_twist;
  received_velocity_msg_ptr_.set(std::make_shared<Twist>(empty_twist));

  // Fill last two commands with default constructed commands
  previous_commands_.emplace(empty_twist);
  previous_commands_.emplace(empty_twist);

  // initialize command subscriber
  if (use_stamped_vel_)
  {
    velocity_command_subscriber_ = node_->create_subscription<Twist>(
      DEFAULT_COMMAND_TOPIC, rclcpp::SystemDefaultsQoS(),
      [this](const std::shared_ptr<Twist> msg) -> void {
        if (!subscriber_is_active_)
        {
          RCLCPP_WARN(node_->get_logger(), "Can't accept new commands. subscriber is inactive");
          return;
        }
        if ((msg->header.stamp.sec == 0) && (msg->header.stamp.nanosec == 0))
        {
          RCLCPP_WARN_ONCE(
            node_->get_logger(),
            "Received TwistStamped with zero timestamp, setting it to current "
            "time, this message will only be shown once");
          msg->header.stamp = node_->get_clock()->now();
        }
        received_velocity_msg_ptr_.set(std::move(msg));
      });
  }
  else
  {
    velocity_command_unstamped_subscriber_ = node_->create_subscription<geometry_msgs::msg::Twist>(
      DEFAULT_COMMAND_UNSTAMPED_TOPIC, rclcpp::SystemDefaultsQoS(),
      [this](const std::shared_ptr<geometry_msgs::msg::Twist> msg) -> void {
        if (!subscriber_is_active_)
        {
          RCLCPP_WARN(node_->get_logger(), "Can't accept new commands. subscriber is inactive");
          return;
        }

        // Write fake header in the stored stamped command
        std::shared_ptr<Twist> twist_stamped;
        received_velocity_msg_ptr_.get(twist_stamped);
        twist_stamped->twist = *msg;
        twist_stamped->header.stamp = node_->get_clock()->now();
      });
  }

  // initialize odometry publisher and messasge
  odometry_publisher_ = node_->create_publisher<nav_msgs::msg::Odometry>(
    DEFAULT_ODOMETRY_TOPIC, rclcpp::SystemDefaultsQoS());
  realtime_odometry_publisher_ =
    std::make_shared<realtime_tools::RealtimePublisher<nav_msgs::msg::Odometry>>(
      odometry_publisher_);

  auto & odometry_message = realtime_odometry_publisher_->msg_;
  odometry_message.header.frame_id = odom_params_.odom_frame_id;
  odometry_message.child_frame_id = odom_params_.base_frame_id;

  // limit the publication on the topics /odom and /tf
  publish_rate_ = node_->get_parameter("publish_rate").as_double();
  publish_period_ = rclcpp::Duration::from_seconds(1.0 / publish_rate_);
  previous_publish_timestamp_ = node_->get_clock()->now();

  // initialize odom values zeros
  odometry_message.twist =
    geometry_msgs::msg::TwistWithCovariance(rosidl_runtime_cpp::MessageInitialization::ALL);

  constexpr size_t NUM_DIMENSIONS = 6;
  for (size_t index = 0; index < 6; ++index)
  {
    // 0, 7, 14, 21, 28, 35
    const size_t diagonal_index = NUM_DIMENSIONS * index + index;
    odometry_message.pose.covariance[diagonal_index] = odom_params_.pose_covariance_diagonal[index];
    odometry_message.twist.covariance[diagonal_index] =
      odom_params_.twist_covariance_diagonal[index];
  }

  // initialize transform publisher and message
  odometry_transform_publisher_ = node_->create_publisher<tf2_msgs::msg::TFMessage>(
    DEFAULT_TRANSFORM_TOPIC, rclcpp::SystemDefaultsQoS());
  realtime_odometry_transform_publisher_ =
    std::make_shared<realtime_tools::RealtimePublisher<tf2_msgs::msg::TFMessage>>(
      odometry_transform_publisher_);

  // keeping track of odom and base_link transforms only
  auto & odometry_transform_message = realtime_odometry_transform_publisher_->msg_;
  odometry_transform_message.transforms.resize(1);
  odometry_transform_message.transforms.front().header.frame_id = odom_params_.odom_frame_id;
  odometry_transform_message.transforms.front().child_frame_id = odom_params_.base_frame_id;

  previous_update_timestamp_ = node_->get_clock()->now();
  return CallbackReturn::SUCCESS;
}

CallbackReturn Ack6WDController::on_activate(const rclcpp_lifecycle::State &)
{
  const auto left_wheel_result =
    configure_side_wheel("left", left_wheel_names_, registered_left_wheel_handles_);
  const auto right_wheel_result =
    configure_side_wheel("right", right_wheel_names_, registered_right_wheel_handles_);
  const auto left_steering_result =
    configure_side_steering("left", left_steering_names_, registered_left_steering_handles_);
  const auto right_steering_result =
    configure_side_steering("right", right_steering_names_, registered_right_steering_handles_);

  const auto middle_wheel_result =
    configure_side_wheel("middle", middle_wheel_names_, registered_middle_wheel_handles_);

  if (left_wheel_result == CallbackReturn::ERROR || right_wheel_result == CallbackReturn::ERROR
      || left_steering_result == CallbackReturn::ERROR || right_steering_result == CallbackReturn::ERROR
      || middle_wheel_result == CallbackReturn::ERROR)
  {
    return CallbackReturn::ERROR;
  }

  if (registered_left_wheel_handles_.empty() || registered_right_wheel_handles_.empty())
  {
    RCLCPP_ERROR(
      node_->get_logger(), "Either left wheel interfaces, right wheel interfaces are non existent");
    return CallbackReturn::ERROR;
  }

  if (registered_middle_wheel_handles_.empty())
  {
    RCLCPP_ERROR(
      node_->get_logger(), "Middle wheel interfaces are non existent");
    return CallbackReturn::ERROR;
  }

  if (registered_left_steering_handles_.empty() || registered_right_steering_handles_.empty())
  {
    RCLCPP_ERROR(
      node_->get_logger(), "Either left steering interfaces, right steering interfaces are non existent");
    return CallbackReturn::ERROR;
  }

  is_halted = false;
  subscriber_is_active_ = true;

  RCLCPP_DEBUG(node_->get_logger(), "Subscriber and publisher are now active.");
  return CallbackReturn::SUCCESS;
}

CallbackReturn Ack6WDController::on_deactivate(const rclcpp_lifecycle::State &)
{
  subscriber_is_active_ = false;
  return CallbackReturn::SUCCESS;
}

CallbackReturn Ack6WDController::on_cleanup(const rclcpp_lifecycle::State &)
{
  if (!reset())
  {
    return CallbackReturn::ERROR;
  }

  received_velocity_msg_ptr_.set(std::make_shared<Twist>());
  return CallbackReturn::SUCCESS;
}

CallbackReturn Ack6WDController::on_error(const rclcpp_lifecycle::State &)
{
  if (!reset())
  {
    return CallbackReturn::ERROR;
  }
  return CallbackReturn::SUCCESS;
}

bool Ack6WDController::reset()
{
  odometry_.resetOdometry();

  // release the old queue
  std::queue<Twist> empty;
  std::swap(previous_commands_, empty);

  registered_left_wheel_handles_.clear();
  registered_right_wheel_handles_.clear();
  registered_middle_wheel_handles_.clear();

  registered_left_steering_handles_.clear();
  registered_right_steering_handles_.clear();

  subscriber_is_active_ = false;
  velocity_command_subscriber_.reset();
  velocity_command_unstamped_subscriber_.reset();

  received_velocity_msg_ptr_.set(nullptr);
  is_halted = false;
  return true;
}

CallbackReturn Ack6WDController::on_shutdown(const rclcpp_lifecycle::State &)
{
  return CallbackReturn::SUCCESS;
}

int Ack6WDController::quadrant(double linear, double angular){
  // Quadrant
  // 0 | 1
  // -----
  // 3 | 2

  if (linear > 0) {
    if (angular >= 0) {
      return 0;
    } else {
      return 1;
    }
  } else {
    if (angular > 0) {
      return 2;
    } else {
      return 3;
    }
  }
}

void Ack6WDController::halt()
{
  const auto halt_wheels = [](auto & wheel_handles) {
    for (const auto & wheel_handle : wheel_handles)
    {
      wheel_handle.velocity.get().set_value(0.0);
    }
  };

  halt_wheels(registered_left_wheel_handles_);
  halt_wheels(registered_right_wheel_handles_);

  halt_wheels(registered_middle_wheel_handles_);

  const auto halt_steerings = [](auto & steering_handles) {
    for (const auto & steering_handle : steering_handles)
    {
      steering_handle.position.get().set_value(0.0);
    }
  };

  halt_steerings(registered_left_steering_handles_);
  halt_steerings(registered_right_steering_handles_);
}

CallbackReturn Ack6WDController::configure_side_wheel(
  const std::string & side, const std::vector<std::string> & wheel_names,
  std::vector<WheelHandle> & registered_handles)
{
  auto logger = node_->get_logger();

  if (wheel_names.empty())
  {
    RCLCPP_ERROR(logger, "No '%s' wheel names specified", side.c_str());
    return CallbackReturn::ERROR;
  }

  // register handles
  registered_handles.reserve(wheel_names.size());
  for (const auto & wheel_name : wheel_names)
  {
    const auto state_handle_pos = std::find_if(
      state_interfaces_.cbegin(), state_interfaces_.cend(), [&wheel_name](const auto & interface) {
        return interface.get_name() == wheel_name &&
               interface.get_interface_name() == HW_IF_POSITION;
      });

    if (state_handle_pos == state_interfaces_.cend())
    {
      RCLCPP_ERROR(logger, "Unable to obtain wheel joint state position handle for %s", wheel_name.c_str());
      return CallbackReturn::ERROR;
    }

    const auto state_handle_vel = std::find_if(
      state_interfaces_.cbegin(), state_interfaces_.cend(), [&wheel_name](const auto & interface) {
        return interface.get_name() == wheel_name &&
               interface.get_interface_name() == HW_IF_VELOCITY;
      });

    if (state_handle_vel == state_interfaces_.cend())
    {
      RCLCPP_ERROR(logger, "Unable to obtain wheel joint state velocity handle for %s", wheel_name.c_str());
      return CallbackReturn::ERROR;
    }

    const auto command_handle = std::find_if(
      command_interfaces_.begin(), command_interfaces_.end(),
      [&wheel_name](const auto & interface) {
        return interface.get_name() == wheel_name &&
               interface.get_interface_name() == HW_IF_VELOCITY;
      });

    if (command_handle == command_interfaces_.end())
    {
      RCLCPP_ERROR(logger, "Unable to obtain wheel joint command handle for %s", wheel_name.c_str());
      return CallbackReturn::ERROR;
    }

    registered_handles.emplace_back(
      WheelHandle{std::ref(*state_handle_pos), std::ref(*state_handle_vel), std::ref(*command_handle)});
  }

  return CallbackReturn::SUCCESS;
}

CallbackReturn Ack6WDController::configure_side_steering(
  const std::string & side, const std::vector<std::string> & steering_names,
  std::vector<SteeringHandle> & registered_handles)
{
  auto logger = node_->get_logger();

  if (steering_names.empty())
  {
    RCLCPP_ERROR(logger, "No '%s' steering names specified", side.c_str());
    return CallbackReturn::ERROR;
  }

  // register handles
  registered_handles.reserve(steering_names.size());
  for (const auto & steering_name : steering_names)
  {
    const auto state_handle_pos = std::find_if(
      state_interfaces_.cbegin(), state_interfaces_.cend(), [&steering_name](const auto & interface) {
        return interface.get_name() == steering_name &&
               interface.get_interface_name() == HW_IF_POSITION;
      });

    if (state_handle_pos == state_interfaces_.cend())
    {
      RCLCPP_ERROR(logger, "Unable to obtain joint state position handle for %s", steering_name.c_str());
      return CallbackReturn::ERROR;
    }

    const auto state_handle_vel = std::find_if(
      state_interfaces_.cbegin(), state_interfaces_.cend(), [&steering_name](const auto & interface) {
        return interface.get_name() == steering_name &&
               interface.get_interface_name() == HW_IF_VELOCITY;
      });

    if (state_handle_vel == state_interfaces_.cend())
    {
      RCLCPP_ERROR(logger, "Unable to obtain joint state velocity handle for %s", steering_name.c_str());
      return CallbackReturn::ERROR;
    }

    const auto command_handle = std::find_if(
      command_interfaces_.begin(), command_interfaces_.end(),
      [&steering_name](const auto & interface) {
        return interface.get_name() == steering_name &&
               interface.get_interface_name() == HW_IF_POSITION;
      });

    if (command_handle == command_interfaces_.end())
    {
      RCLCPP_ERROR(logger, "Unable to obtain joint command handle for %s", steering_name.c_str());
      return CallbackReturn::ERROR;
    }

    registered_handles.emplace_back(
      SteeringHandle{std::ref(*state_handle_pos), std::ref(*state_handle_pos), std::ref(*command_handle)});
  }

  return CallbackReturn::SUCCESS;
}
}  // namespace ack_6wd_controller

#include "class_loader/register_macro.hpp"

CLASS_LOADER_REGISTER_CLASS(
  ack_6wd_controller::Ack6WDController, controller_interface::ControllerInterface)
