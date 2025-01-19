is_enable_https = false

Pod::Spec.new do |s|
    s.name         = "slark"

    s.version      = "0.0.1"

    s.summary      = "iOS and Android Video Player"

    s.ios.deployment_target = '16.3'

    s.description  = <<-DESC
        slark is a cross platform player that supports iOS and Android
    DESC
    s.homepage     = "https://github.com/Nevermore1994/slark"
   
    s.license      = { :type => "MIT", :file => "LICENSE" }

    s.author        = { "Nevermore-" => "875229565@qq.com" }

    s.source       = { :git => "https://github.com/Nevermore1994/slark.git", :tag => "#{s.version}" }
    
    s.libraries = 'c++'

    s.source_files  = 'src/base/**/*.hpp',
                      'src/base/**/*.h',
                      'src/base/**/*.cpp',
                      'src/base/**/*.mm',
                    
                      'src/core/**/*.hpp',
                      'src/core/**/*.h',
                      'src/core/**/*.cpp',
                      'src/core/**/*.mm',
                    
                      'src/http/**/*.h',
                      'src/http/**/*.hpp',
                      'src/http/**/*.cpp',
                    
                      'src/interface/iOS/*.h',
                      'src/interface/iOS/*.cpp',
                      'src/interface/iOS/*.mm'

    s.public_header_files = 'src/interface/*.h'  

    s.requires_arc = true
    
    s.pod_target_xcconfig = { 'ONLY_ACTIVE_ARCH' => 'YES' }

    s.user_target_xcconfig = { 
        'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'arm64'
    }

    if is_enable_https
        s.libraries = 'ssl', 'crypto'
    end

    s.pod_target_xcconfig = { 
        'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'arm64',
        'HEADER_SEARCH_PATHS' => ' "$(inherited)" "$(PODS_TARGET_SRCROOT)/include" "/opt/homebrew/opt/openssl/include/" ',
        'LIBRARY_SEARCH_PATHS' => '/opt/homebrew/opt/openssl/lib',
        'USER_HEADER_SEARCH_PATHS' => '"$(PODS_TARGET_SRCROOT)"'\
            '"$(PODS_TARGET_SRCROOT)/base"' \
            '"$(PODS_TARGET_SRCROOT)/base/platform/iOS"' \
            '"$(PODS_TARGET_SRCROOT)/core"' \
            '"$(PODS_TARGET_SRCROOT)/core/audio"' \
            '"$(PODS_TARGET_SRCROOT)/core/codec"' \
            '"$(PODS_TARGET_SRCROOT)/core/muxer"' \
            '"$(PODS_TARGET_SRCROOT)/core/platform/iOS"' \
            '"$(PODS_TARGET_SRCROOT)/http/include"' \
            '"$(PODS_TARGET_SRCROOT)/http/include/public"' \
            '"$(PODS_TARGET_SRCROOT)/interface/platform/iOS"'
    }

    enable_https_flag = is_enable_https ? 1 : 0

    s.xcconfig = { 
        'CLANG_C_LANGUAGE_STANDARD' => 'c17',
        'CLANG_CXX_LANGUAGE_STANDARD' => 'c++23',
        "GCC_PREPROCESSOR_DEFINITIONS" => "SLARK_IOS=1 GLES_SILENCE_DEPRECATION=1 ENABLE_HTTPS=#{enable_https_flag}",
        'OTHER_CPLUSPLUSFLAGS' => '-Wall -Wextra -Wpedantic -Wcast-align -Wcast-qual -Wconversion -Wdisabled-optimization -Wendif-labels -Wfloat-equal -Winit-self -Winline -Wmissing-include-dirs -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wpacked -Wpointer-arith -Wredundant-decls -Wshadow -Wsign-promo -Wvariadic-macros -Wwrite-strings -Wno-variadic-macros -Wno-unknown-pragmas'
    }
    s.frameworks = 'Foundation', 'AVFoundation', 'VideoToolBox', 'AudioToolBox', 'CoreMedia'
  end

