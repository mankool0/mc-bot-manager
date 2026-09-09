# Puts the client mod jars the manager bundles into OUT_DIR
#
# Inputs:
#   CLIENT_DIR       the client/ source tree, holding gradlew
#   OUT_DIR          where the jars are collected
#   MC_VERSIONS      comma separated Minecraft versions
#   MANAGER_VERSION  the manager's version, which the mod must match
#   JARS_DIR         optional directory of prebuilt jars; skips Gradle entirely

string(REPLACE "," ";" MC_VERSION_LIST "${MC_VERSIONS}")

if(JARS_DIR)
    set(JAR_SOURCE_DIR "${JARS_DIR}")
else()
    if(CMAKE_HOST_WIN32)
        set(GRADLEW "${CLIENT_DIR}/gradlew.bat")
    else()
        set(GRADLEW "${CLIENT_DIR}/gradlew")
    endif()

    foreach(mc_version ${MC_VERSION_LIST})
        message(STATUS "Building client mod for Minecraft ${mc_version}")
        # mod_version is passed rather than read from gradle.properties so the
        # jar always carries the manager's version
        execute_process(
            COMMAND "${GRADLEW}" build
                    "-Pminecraft_version=${mc_version}"
                    "-Pmod_version=${MANAGER_VERSION}"
            WORKING_DIRECTORY "${CLIENT_DIR}"
            RESULT_VARIABLE gradle_result
        )
        if(NOT gradle_result EQUAL 0)
            message(FATAL_ERROR
                "Gradle failed to build the client mod for Minecraft ${mc_version}.\n"
                "Configure with -DBUNDLE_CLIENT_MOD=OFF to build the manager without it, "
                "or with -DCLIENT_MOD_JARS_DIR=<dir> to use prebuilt jars.")
        endif()
    endforeach()

    set(JAR_SOURCE_DIR "${CLIENT_DIR}/build/libs")
endif()

# Jars from an earlier manager version are ignored at runtime
file(GLOB stale_jars "${OUT_DIR}/mc-bot-client-*.jar")
foreach(stale ${stale_jars})
    file(REMOVE "${stale}")
endforeach()
file(MAKE_DIRECTORY "${OUT_DIR}")

foreach(mc_version ${MC_VERSION_LIST})
    set(jar_name "mc-bot-client-${mc_version}-${MANAGER_VERSION}.jar")
    set(jar_path "${JAR_SOURCE_DIR}/${jar_name}")
    if(NOT EXISTS "${jar_path}")
        message(FATAL_ERROR
            "Expected ${jar_name} in ${JAR_SOURCE_DIR}, but it is not there.\n"
            "The client mod version must match the manager version (${MANAGER_VERSION}); "
            "bump_version.sh keeps manager/CMakeLists.txt and client/gradle.properties together.")
    endif()
    file(COPY "${jar_path}" DESTINATION "${OUT_DIR}")
endforeach()

message(STATUS "Bundled client mod ${MANAGER_VERSION} for Minecraft ${MC_VERSIONS}")
