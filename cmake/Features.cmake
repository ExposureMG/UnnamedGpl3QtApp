# Optional backends. Each one needs its git submodule in extern/ and is OFF by
# default until its milestone lands (see docs/ROADMAP.md). The core library and
# the GUI always build without them.
#
#   git submodule update --init extern/<name>
#
# unnamed_enable_feature(<OPTION> <extern dir> <description>)
function(unnamed_enable_feature option extern_dir description)
    option(${option} "${description}" OFF)
    if(${option})
        if(NOT EXISTS "${PROJECT_SOURCE_DIR}/${extern_dir}/CMakeLists.txt")
            message(FATAL_ERROR
                "${option}=ON needs ${extern_dir}, which is empty.\n"
                "Run: git submodule update --init ${extern_dir}")
        endif()
        set(UNNAMED_FEATURES "${UNNAMED_FEATURES};${option}" PARENT_SCOPE)
    endif()
endfunction()

set(UNNAMED_FEATURES "")
unnamed_enable_feature(UNNAMED_WITH_FATX extern/FATX     "FATX filesystem (images and drives)")
unnamed_enable_feature(UNNAMED_WITH_XEX  extern/XexTool  "XEX inspect/decrypt/compress/patch/sign")
unnamed_enable_feature(UNNAMED_WITH_STFS extern/gxbuild3 "STFS packages (needs gxbuild3 files relicensed, see docs/ROADMAP.md)")
# XBDM (a console over the network) uses UpdClient's protocol client in
# extern/UpdClient. It is ON by default, so a clone without that submodule
# configures without XBDM instead of failing.
option(UNNAMED_WITH_XBDM "Xbox debug monitor client (network), through extern/UpdClient" ON)
if(UNNAMED_WITH_XBDM)
    if(EXISTS "${PROJECT_SOURCE_DIR}/extern/UpdClient/CMakeLists.txt")
        list(APPEND UNNAMED_FEATURES UNNAMED_WITH_XBDM)
    else()
        message(STATUS "UNNAMED_WITH_XBDM: extern/UpdClient is empty, building without XBDM. "
                       "Run: git submodule update --init extern/UpdClient")
        set(UNNAMED_WITH_XBDM OFF)
    endif()
endif()

if(UNNAMED_FEATURES)
    message(STATUS "Enabled backends: ${UNNAMED_FEATURES}")
else()
    message(STATUS "Enabled backends: (none)")
endif()
