cmake_minimum_required(VERSION 3.24)
project(Spotter VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(MSVC)
  # Warnings in our code are errors; third-party headers (included with <>) are not checked.
  add_compile_options(/W4 /WX /permissive- /utf-8 /wd4251 /external:anglebrackets /external:W0)
  add_compile_definitions(NOMINMAX _USE_MATH_DEFINES)
else()
  add_compile_options(-Wall -Wextra -Werror)
endif()

option(SPOTTER_APP "Build the desktop app (Qt Quick)" ON)
set(THIRD_PARTY ${CMAKE_SOURCE_DIR}/third_party)

# OpenCV: third_party/opencv/build on Windows (scripts/fetch-deps.sh), the system's elsewhere.
if(WIN32 AND NOT OpenCV_DIR)
  set(OpenCV_DIR ${THIRD_PARTY}/opencv/build)
endif()
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio)

# ONNX Runtime: the DirectML build on Windows, the CPU build on Linux (scripts/fetch-deps.sh).
set(ORT ${THIRD_PARTY}/onnxruntime)
add_library(onnxruntime SHARED IMPORTED)
if(WIN32)
  set_target_properties(onnxruntime PROPERTIES IMPORTED_LOCATION ${ORT}/lib/onnxruntime.dll IMPORTED_IMPLIB ${ORT}/lib/onnxruntime.lib)
  set(RUNTIME_DLLS ${ORT}/lib/onnxruntime.dll ${ORT}/lib/onnxruntime_providers_shared.dll ${ORT}/lib/DirectML.dll
      ${OpenCV_DIR}/x64/vc16/bin/opencv_world4120.dll ${OpenCV_DIR}/x64/vc16/bin/opencv_videoio_ffmpeg4120_64.dll)
else()
  file(GLOB ORT_SO ${ORT}/lib/libonnxruntime.so.*.*.*)
  set_target_properties(onnxruntime PROPERTIES IMPORTED_LOCATION ${ORT_SO} IMPORTED_SONAME libonnxruntime.so.1)
endif()
target_include_directories(onnxruntime INTERFACE ${ORT}/include)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Sql Network Test)

# Everything that does not need a screen: pose, features, the classifier, repetitions, form checks,
# the workout log and the planner.
add_library(spotter_core STATIC
  core/pose.cpp core/skeleton.cpp core/classifier.cpp core/reps.cpp core/form.cpp core/session.cpp
  core/log.cpp core/planner.cpp core/llm.cpp core/speech.cpp)
target_include_directories(spotter_core PUBLIC core)
target_link_libraries(spotter_core PUBLIC ${OpenCV_LIBS} onnxruntime Qt6::Core Qt6::Sql Qt6::Network)
if(WIN32)
  target_compile_definitions(spotter_core PRIVATE SPOTTER_DML)
  target_link_libraries(spotter_core PUBLIC ole32 sapi dxgi)
endif()
target_compile_definitions(spotter_core PUBLIC SPOTTER_ROOT="${CMAKE_SOURCE_DIR}")

add_executable(spotter-cli cli/main.cpp cli/plan_eval.cpp)
target_link_libraries(spotter-cli PRIVATE spotter_core)

enable_testing()
foreach(t skeleton reps form log planner pose)
  add_executable(test_${t} tests/test_${t}.cpp)
  target_link_libraries(test_${t} PRIVATE spotter_core Qt6::Test)
  add_test(NAME ${t} COMMAND test_${t})
  if(WIN32)  # Qt's DLLs (and with them its plugins) from the Qt the build used
    set_tests_properties(${t} PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QT6_INSTALL_PREFIX}/bin")
  endif()
endforeach()

if(SPOTTER_APP)
  find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Quick Qml QuickControls2)
  qt_standard_project_setup(REQUIRES 6.8)
  set_source_files_properties(app/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
  qt_add_executable(spotter WIN32 app/main.cpp app/coach.cpp app/frames.cpp app/history.cpp)
  qt_add_qml_module(spotter URI Spotter VERSION 1.0
    QML_FILES app/qml/Main.qml app/qml/TrainView.qml app/qml/PlanView.qml app/qml/ProgressView.qml
              app/qml/SkeletonOverlay.qml app/qml/LineChart.qml app/qml/Theme.qml)
  target_include_directories(spotter PRIVATE app)
  if(MSVC)  # "unreachable code" inside Qt's templates, reported where they are instantiated
    target_compile_options(spotter PRIVATE /wd4702)
  endif()
  target_link_libraries(spotter PRIVATE spotter_core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::QuickControls2)
  set(SPOTTER_TARGETS spotter-cli spotter)
else()
  set(SPOTTER_TARGETS spotter-cli)
endif()

if(WIN32)
  foreach(t ${SPOTTER_TARGETS} test_skeleton test_reps test_form test_log test_planner test_pose)
    add_custom_command(TARGET ${t} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RUNTIME_DLLS} $<TARGET_FILE_DIR:${t}>)
  endforeach()
endif()
