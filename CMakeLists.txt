cmake_minimum_required(VERSION 3.5)
project(ack_6wd_controller)

# Default to C++14
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 14)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra)
endif()

find_package(ament_cmake REQUIRED)
find_package(controller_interface REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(hardware_interface REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(pluginlib REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(realtime_tools REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_msgs REQUIRED)

add_library(ack_6wd_controller SHARED
  src/ack_6wd_controller.cpp
  src/odometry.cpp
  src/speed_limiter.cpp
)

target_include_directories(ack_6wd_controller PRIVATE include)
ament_target_dependencies(ack_6wd_controller
  builtin_interfaces
  controller_interface
  geometry_msgs
  hardware_interface
  nav_msgs
  pluginlib
  rclcpp
  rclcpp_lifecycle
  realtime_tools
  tf2
  tf2_msgs
)
# Causes the visibility macros to use dllexport rather than dllimport,
# which is appropriate when building the dll but not consuming it.
target_compile_definitions(ack_6wd_controller PRIVATE "ACK_6WD_CONTROLLER_BUILDING_DLL")
# prevent pluginlib from using boost
target_compile_definitions(ack_6wd_controller PUBLIC "PLUGINLIB__DISABLE_BOOST_FUNCTIONS")
pluginlib_export_plugin_description_file(controller_interface ack_6wd_plugin.xml)

install(DIRECTORY include/
  DESTINATION include
)

install(TARGETS ack_6wd_controller
  RUNTIME DESTINATION bin
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
)

ament_export_dependencies(
  controller_interface
  geometry_msgs
  hardware_interface
  rclcpp
  rclcpp_lifecycle
  tf2
  tf2_msgs
)
ament_export_include_directories(
  include
)
ament_export_libraries(
  ack_6wd_controller
)
ament_package()
