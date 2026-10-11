# Optional Qt demo-only models (not part of the installed library).
# Assets are cached in this build directory and downloaded only when missing.
set(_speech_dir "${CMAKE_CURRENT_BINARY_DIR}/demo-models/speech")
file(MAKE_DIRECTORY "${_speech_dir}")

function(_libaudition_demo_speech_download name url sha256)
    set(_destination "${_speech_dir}/${name}")
    set(_need_download TRUE)
    if(EXISTS "${_destination}")
        file(SIZE "${_destination}" _size)
        if(_size GREATER 0)
            if(NOT "${sha256}" STREQUAL "")
                file(SHA256 "${_destination}" _actual_hash)
                if(_actual_hash STREQUAL sha256)
                    set(_need_download FALSE)
                else()
                    message(WARNING "Invalid cached hash for ${name}; downloading again")
                endif()
            else()
                set(_need_download FALSE)
            endif()
        endif()
    endif()
    if(_need_download)
        message(STATUS "Downloading speech demo model: ${name}")
        file(DOWNLOAD "${url}" "${_destination}.part"
            STATUS _status TLS_VERIFY ON SHOW_PROGRESS
            INACTIVITY_TIMEOUT 90 TIMEOUT 1800)
        list(GET _status 0 _code)
        if(NOT _code EQUAL 0)
            file(REMOVE "${_destination}.part")
            list(GET _status 1 _reason)
            message(FATAL_ERROR "Failed downloading ${name}: ${_reason}. "
                "For offline builds set LIBAUDITION_DEMO_FETCH_SPEECH_MODELS=OFF "
                "and provide local model paths in the demo.")
        endif()
        file(SIZE "${_destination}.part" _size)
        if(_size EQUAL 0)
            file(REMOVE "${_destination}.part")
            message(FATAL_ERROR "Downloaded empty speech demo asset: ${name}")
        endif()
        if(NOT "${sha256}" STREQUAL "")
            file(SHA256 "${_destination}.part" _actual_hash)
            if(NOT _actual_hash STREQUAL sha256)
                file(REMOVE "${_destination}.part")
                message(FATAL_ERROR "Speech demo model checksum mismatch: ${name}")
            endif()
        endif()
        file(RENAME "${_destination}.part" "${_destination}")
    endif()
endfunction()

# sherpa-onnx upstream Silero ONNX release asset.
_libaudition_demo_speech_download(silero_vad.onnx
    "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/silero_vad.onnx"
    "")

# sherpa-compatible SenseVoiceSmall INT8 with metadata and matching token table.
# SHA256 checksums published in the model card:
# https://huggingface.co/nocmt/SenseVoiceSmall-INT8-ONNX
set(_sensevoice_base "https://huggingface.co/nocmt/SenseVoiceSmall-INT8-ONNX/resolve/main")
_libaudition_demo_speech_download(sensevoice.int8.onnx
    "${_sensevoice_base}/model.int8.onnx"
    "c71f0ce00bec95b07744e116345e33d8cbbe08cef896382cf907bf4b51a2cd51")
_libaudition_demo_speech_download(sensevoice.tokens.txt
    "${_sensevoice_base}/tokens.txt"
    "f449eb28dc567533d7fa59be34e2abca8784f771850c78a47fb731a31429a1dc")

set(LIBAUDITION_DEMO_SILERO_VAD_MODEL "${_speech_dir}/silero_vad.onnx")
set(LIBAUDITION_DEMO_SENSEVOICE_MODEL "${_speech_dir}/sensevoice.int8.onnx")
set(LIBAUDITION_DEMO_SENSEVOICE_TOKENS "${_speech_dir}/sensevoice.tokens.txt")
