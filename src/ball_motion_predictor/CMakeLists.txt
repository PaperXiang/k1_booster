cmake_minimum_required(VERSION 3.10)
project(ball_motion_predictor)

find_package(ament_cmake REQUIRED)
include(CTest)

add_library(${PROJECT_NAME}
  src/ball_motion_predictor.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

install(TARGETS ${PROJECT_NAME}
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(DIRECTORY include/
  DESTINATION include
)

ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_libraries(${PROJECT_NAME})
ament_export_include_directories(include)

if(BUILD_TESTING)
  add_executable(test_ball_motion_predictor
    test/test_ball_motion_predictor.cpp
  )
  target_link_libraries(test_ball_motion_predictor ${PROJECT_NAME})
  add_test(NAME test_ball_motion_predictor COMMAND test_ball_motion_predictor)
endif()

ament_package()
