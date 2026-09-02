file(READ "${SOURCE_DIR}/src/diagnostics/Diagnostics.cpp" REPORT_SOURCE)
set(SENSITIVE_REPORT_APIS
    "hostName(" "userName(" "homePath(" "systemEnvironment().toStringList"
    "clipboard(" "applicationPid(" "displayName" "eventPath" "sysfsName")
foreach(PATTERN IN LISTS SENSITIVE_REPORT_APIS)
    string(FIND "${REPORT_SOURCE}" "${PATTERN}" MATCH_OFFSET)
    if(NOT MATCH_OFFSET EQUAL -1)
        message(FATAL_ERROR "Sensitive debug-report category found: ${PATTERN}")
    endif()
endforeach()
message(STATUS "Safe diagnostics source uses no known sensitive category APIs")
