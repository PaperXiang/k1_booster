cmake_minimum_required(VERSION 3.16...3.27)
project(k1_ball_intercept)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)

# 纯头文件逻辑库 (追逐拦截解算), 无 ROS / brain / Eigen 依赖, 便于独立测试与复用。
# 使用 INTERFACE 库: 只导出头文件与编译要求, 不产出二进制。
add_library(${PROJECT_NAME} INTERFACE)

target_include_directories(${PROJECT_NAME} INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_17)

install(TARGETS ${PROJECT_NAME} EXPORT export_${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)

ament_export_targets(export_${PROJECT_NAME})
ament_export_include_directories(include)

# 注: 拦截解算的单元测试在 test/test_intercept_time.cpp, 故意不接入 colcon, 手动编译运行:
#   g++ -std=c++17 -Isrc/k1_ball_intercept/include src/k1_ball_intercept/test/test_intercept_time.cpp -o /tmp/test_intercept && /tmp/test_intercept
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  set(ament_cmake_copyright_FOUND TRUE)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
