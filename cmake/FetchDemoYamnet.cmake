# Download only the optional Qt demo model; no model files are installed
# or embedded into the libaudition library.
set(_yamnet_revision "f25b741")
set(_yamnet_base "https://huggingface.co/audiomagic/yamnet-onnx/resolve/${_yamnet_revision}")
set(_yamnet_dir "${CMAKE_CURRENT_BINARY_DIR}/demo-models/yamnet")
file(MAKE_DIRECTORY "${_yamnet_dir}")

foreach(_asset IN ITEMS yamnet.onnx yamnet_class_map.csv)
    set(_destination "${_yamnet_dir}/${_asset}")
    if(NOT EXISTS "${_destination}")
        message(STATUS "Downloading optional YAMNet Qt demo asset: ${_asset}")
        file(DOWNLOAD "${_yamnet_base}/${_asset}" "${_destination}.part"
            STATUS _download_status TLS_VERIFY ON SHOW_PROGRESS
            INACTIVITY_TIMEOUT 60 TIMEOUT 300)
        list(GET _download_status 0 _status_code)
        if(NOT _status_code EQUAL 0)
            file(REMOVE "${_destination}.part")
            list(GET _download_status 1 _status_message)
            message(FATAL_ERROR
                "Unable to download YAMNet demo asset ${_asset}: ${_status_message}. "
                "Set LIBAUDITION_DEMO_FETCH_YAMNET_MODEL=OFF for an offline build.")
        endif()
        file(RENAME "${_destination}.part" "${_destination}")
    endif()
endforeach()

# TODO: pin and verify SHA256 checksums after independently verifying both
# upstream artifacts; a pinned commit alone is not integrity verification.
set(LIBAUDITION_DEMO_YAMNET_MODEL "${_yamnet_dir}/yamnet.onnx")
set(LIBAUDITION_DEMO_YAMNET_LABELS "${_yamnet_dir}/yamnet_class_map.csv")
