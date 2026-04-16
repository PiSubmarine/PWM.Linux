include("${PISUBMARINE_BUILD_SYSTEM}")

message("Adding library: ${PISUBMARINE_MODULE_NAME}")
add_library(${PISUBMARINE_MODULE_NAME}
        PiSubmarine/PWM/Linux/Driver.cpp
        PiSubmarine/PWM/Linux/ErrorCode.cpp)
target_include_directories(${PISUBMARINE_MODULE_NAME} PUBLIC "../public")
target_include_directories(${PISUBMARINE_MODULE_NAME} PRIVATE ".")
PiSubmarineInitTarget(${PISUBMARINE_MODULE_NAME})

PiSubmarineAddDependency("https://github.com/PiSubmarine/PWM.Api.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME} PiSubmarine.PWM.Api)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Error.Api.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME} PiSubmarine.Error.Api)
