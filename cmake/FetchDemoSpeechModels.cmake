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

if(LIBAUDITION_DEMO_FETCH_SPEECH_MODELS)
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
endif()


# Large optional model packs. Only extract when required files are missing;
# do not redownload on every CMake configure.
if(LIBAUDITION_DEMO_FETCH_EXTRA_SPEECH_MODELS)
    _libaudition_demo_speech_download(ten-vad.onnx
        "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/ten-vad.onnx"
        "")

    function(_libaudition_extract_demo_pack archive package file_pairs)
        # file_pairs: source-relative|target-relative elements encoded with a
        # comma to avoid the CMake list separator in function arguments.
        set(_ready TRUE)
        foreach(_pair IN LISTS file_pairs)
            string(REPLACE "," ";" _parts "${_pair}")
            list(GET _parts 1 _target)
            if(NOT EXISTS "${_speech_dir}/${_target}")
                set(_ready FALSE)
            endif()
        endforeach()
        if(_ready)
            return()
        endif()
        set(_unpack_dir "${_speech_dir}/_unpack_${package}")
        file(REMOVE_RECURSE "${_unpack_dir}")
        file(MAKE_DIRECTORY "${_unpack_dir}")
        file(ARCHIVE_EXTRACT INPUT "${_speech_dir}/${archive}"
             DESTINATION "${_unpack_dir}")
        foreach(_pair IN LISTS file_pairs)
            string(REPLACE "," ";" _parts "${_pair}")
            list(GET _parts 0 _source)
            list(GET _parts 1 _target)
            set(_original "${_unpack_dir}/${package}/${_source}")
            if(NOT EXISTS "${_original}")
                message(FATAL_ERROR "Model archive ${archive} is missing ${_source}")
            endif()
            get_filename_component(_parent "${_speech_dir}/${_target}" DIRECTORY)
            file(MAKE_DIRECTORY "${_parent}")
            configure_file("${_original}" "${_speech_dir}/${_target}" COPYONLY)
        endforeach()
        file(REMOVE_RECURSE "${_unpack_dir}")
    endfunction()

    # Upstream multilingual tiny Whisper, including Russian; use INT8 files
    # from the official sherpa-onnx archive to reduce disk/runtime footprint.
    set(_whisper_pack sherpa-onnx-whisper-tiny)
    _libaudition_demo_speech_download("${_whisper_pack}.tar.bz2"
        "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/${_whisper_pack}.tar.bz2"
        "")
    _libaudition_extract_demo_pack("${_whisper_pack}.tar.bz2" "${_whisper_pack}"
        "tiny-encoder.int8.onnx,whisper/encoder.onnx;tiny-decoder.int8.onnx,whisper/decoder.onnx;tiny-tokens.txt,whisper/tokens.txt")
    
    # Official English Paraformer offline package; no Russian support.
    set(_paraformer_pack sherpa-onnx-paraformer-en-2024-03-09)
    _libaudition_demo_speech_download("${_paraformer_pack}.tar.bz2"
        "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/${_paraformer_pack}.tar.bz2"
        "")
    _libaudition_extract_demo_pack("${_paraformer_pack}.tar.bz2" "${_paraformer_pack}"
        "model.int8.onnx,paraformer/model.onnx;tokens.txt,paraformer/tokens.txt")

    # Russian NeMo CTC (GigaAM v2).
    set(_nemo_pack sherpa-onnx-nemo-ctc-giga-am-v2-russian-2025-04-19)
    _libaudition_demo_speech_download("${_nemo_pack}.tar.bz2"
        "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/${_nemo_pack}.tar.bz2" "")
    _libaudition_extract_demo_pack("${_nemo_pack}.tar.bz2" "${_nemo_pack}"
        "model.int8.onnx,nemo_ctc/model.onnx;tokens.txt,nemo_ctc/tokens.txt")

    # Chinese Zipformer CTC and WeNet CTC for backend comparison.
    set(_zip_pack sherpa-onnx-zipformer-ctc-zh-int8-2025-07-03)
    _libaudition_demo_speech_download("${_zip_pack}.tar.bz2"
        "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/${_zip_pack}.tar.bz2" "")
    _libaudition_extract_demo_pack("${_zip_pack}.tar.bz2" "${_zip_pack}"
        "model.int8.onnx,zipformer_ctc/model.onnx;tokens.txt,zipformer_ctc/tokens.txt")

    set(_wenet_pack sherpa-onnx-wenetspeech-wu-u2pp-conformer-ctc-zh-int8-2026-02-03)
    _libaudition_demo_speech_download("${_wenet_pack}.tar.bz2"
        "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/${_wenet_pack}.tar.bz2" "")
    _libaudition_extract_demo_pack("${_wenet_pack}.tar.bz2" "${_wenet_pack}"
        "model.int8.onnx,wenet_ctc/model.onnx;tokens.txt,wenet_ctc/tokens.txt")

    # Russian Zipformer transducer; this uses the offline transducer API.
    set(_transducer_pack sherpa-onnx-zipformer-ru-2024-09-18)
    _libaudition_demo_speech_download("${_transducer_pack}.tar.bz2"
        "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/${_transducer_pack}.tar.bz2" "")
    _libaudition_extract_demo_pack("${_transducer_pack}.tar.bz2" "${_transducer_pack}"
        "encoder.int8.onnx,transducer/encoder.onnx;decoder.int8.onnx,transducer/decoder.onnx;joiner.int8.onnx,transducer/joiner.onnx;tokens.txt,transducer/tokens.txt")
endif()
