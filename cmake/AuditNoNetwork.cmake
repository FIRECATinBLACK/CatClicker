file(GLOB_RECURSE CATCLICKER_RUNTIME_SOURCE
    "${SOURCE_DIR}/src/*.cpp" "${SOURCE_DIR}/src/*.h" "${SOURCE_DIR}/qml/*.qml")
set(CATCLICKER_NETWORK_PATTERNS
    "QNetworkAccessManager" "QTcpSocket" "QUdpSocket" "libcurl")
foreach(CATCLICKER_FILE IN LISTS CATCLICKER_RUNTIME_SOURCE)
    file(READ "${CATCLICKER_FILE}" CATCLICKER_CONTENT)
    foreach(CATCLICKER_PATTERN IN LISTS CATCLICKER_NETWORK_PATTERNS)
        string(FIND "${CATCLICKER_CONTENT}" "${CATCLICKER_PATTERN}" CATCLICKER_MATCH)
        if(NOT CATCLICKER_MATCH EQUAL -1)
            message(FATAL_ERROR "Runtime network API '${CATCLICKER_PATTERN}' found in ${CATCLICKER_FILE}")
        endif()
    endforeach()
endforeach()
message(STATUS "No prohibited runtime network APIs found")
