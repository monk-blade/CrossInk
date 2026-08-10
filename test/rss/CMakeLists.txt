add_executable(RssPolicyTest RssPolicyTest.cpp)
target_link_libraries(RssPolicyTest PRIVATE crosspoint_test_common gtest_main)
gtest_discover_tests(RssPolicyTest)

add_executable(RssItemStateStoreTest
  RssItemStateStoreTest.cpp
  stubs/HalStorage.cpp
  ${REPO_ROOT}/src/RssItemStateStore.cpp
)
target_include_directories(RssItemStateStoreTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Serialization
)
target_link_libraries(RssItemStateStoreTest PRIVATE crosspoint_test_common gtest_main)
gtest_discover_tests(RssItemStateStoreTest)

find_package(EXPAT REQUIRED)
add_executable(RssParserTest
  RssParserTest.cpp
  ${REPO_ROOT}/lib/RssParser/RssParser.cpp
)
target_include_directories(RssParserTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/RssParser
  ${REPO_ROOT}/lib/XmlParserUtils
)
target_link_libraries(RssParserTest PRIVATE crosspoint_test_common gtest_main EXPAT::EXPAT)
gtest_discover_tests(RssParserTest)

add_executable(RssItemCacheTest
  RssItemCacheTest.cpp
  stubs/HalStorage.cpp
  ${REPO_ROOT}/src/RssItemCache.cpp
)
target_include_directories(RssItemCacheTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/RssParser
  ${REPO_ROOT}/lib/Utf8
)
target_link_libraries(RssItemCacheTest PRIVATE crosspoint_test_common gtest_main)
gtest_discover_tests(RssItemCacheTest)

add_executable(FreshRssCacheTest
  FreshRssCacheTest.cpp
  stubs/HalStorage.cpp
  ${REPO_ROOT}/src/FreshRssCache.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(FreshRssCacheTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/RssParser
  ${REPO_ROOT}/lib/Utf8
)
target_link_libraries(FreshRssCacheTest PRIVATE crosspoint_test_common gtest_main)
gtest_discover_tests(FreshRssCacheTest)

add_executable(FreshRssJsonParserTest
  FreshRssJsonParserTest.cpp
  stubs/network/HttpDownloader.cpp
  ${REPO_ROOT}/src/FreshRssApiClient.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)
target_include_directories(FreshRssJsonParserTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/JsonParser
)
target_compile_options(FreshRssJsonParserTest PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/stubs/StreamForward.h
)
target_link_libraries(FreshRssJsonParserTest PRIVATE crosspoint_test_common gtest_main)
gtest_discover_tests(FreshRssJsonParserTest)

add_executable(FreshRssApiClientTest
  FreshRssApiClientTest.cpp
  stubs/network/HttpDownloader.cpp
  ${REPO_ROOT}/src/FreshRssApiClient.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)
target_include_directories(FreshRssApiClientTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/JsonParser
)
target_compile_options(FreshRssApiClientTest PRIVATE
  -include ${CMAKE_CURRENT_SOURCE_DIR}/stubs/StreamForward.h
)
target_link_libraries(FreshRssApiClientTest PRIVATE crosspoint_test_common gtest_main)
gtest_discover_tests(FreshRssApiClientTest)

add_executable(ReaderLayoutTest ReaderLayoutTest.cpp)
target_link_libraries(ReaderLayoutTest PRIVATE crosspoint_test_common gtest_main)
gtest_discover_tests(ReaderLayoutTest)
