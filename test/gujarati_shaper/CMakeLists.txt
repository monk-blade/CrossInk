add_executable(GujaratiShaperTest
  GujaratiShaperTest.cpp
  ${REPO_ROOT}/lib/GujaratiShaper/GujaratiShaper.cpp
)

target_include_directories(GujaratiShaperTest PRIVATE
  ${REPO_ROOT}/lib/GujaratiShaper
)

target_link_libraries(GujaratiShaperTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GujaratiShaperTest)

add_executable(GujaratiIntegrationTest
  GujaratiIntegrationTest.cpp
  ${REPO_ROOT}/lib/GujaratiShaper/GujaratiShaper.cpp
  ${REPO_ROOT}/lib/GujaratiShaper/GujaratiIntegration.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(GujaratiIntegrationTest PRIVATE
  ${REPO_ROOT}/lib/GujaratiShaper
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(GujaratiIntegrationTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GujaratiIntegrationTest)
