cmake_minimum_required(VERSION 3.22)
project(PlanTasks LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets Network Sql Concurrent)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LibIcal REQUIRED IMPORTED_TARGET libical>=3.0)
pkg_check_modules(LibSecret REQUIRED IMPORTED_TARGET libsecret-1)

add_library(plan-core src/store.cpp src/caldav.cpp)
target_link_libraries(plan-core PUBLIC Qt6::Core Qt6::Network Qt6::Sql PkgConfig::LibIcal)
target_include_directories(plan-core PUBLIC src)

add_executable(plan-tasks src/main.cpp src/window.cpp src/appearance.cpp src/preferences.cpp src/credentials.cpp src/projects.cpp)
string(TIMESTAMP PLAN_TASKS_BUILD "%Y-%m-%d %H:%M UTC" UTC)
target_compile_definitions(plan-tasks PRIVATE PLAN_TASKS_BUILD="${PLAN_TASKS_BUILD}")
target_link_libraries(plan-tasks PRIVATE plan-core Qt6::Widgets Qt6::Concurrent PkgConfig::LibSecret)

install(TARGETS plan-tasks RUNTIME DESTINATION bin)
install(FILES data/local.plan.tasks.desktop DESTINATION share/applications)
install(FILES data/local.plan.tasks.metainfo.xml DESTINATION share/metainfo)
install(FILES data/local.plan.tasks.svg DESTINATION share/icons/hicolor/scalable/apps)

include(CTest)
if(BUILD_TESTING)
    add_executable(plan-tests tests/core.cpp)
    target_link_libraries(plan-tests PRIVATE plan-core)
    add_test(NAME core COMMAND plan-tests)
    add_test(NAME window COMMAND plan-tasks --smoke-test)
    set_tests_properties(window PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
