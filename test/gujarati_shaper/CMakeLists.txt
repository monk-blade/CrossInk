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
