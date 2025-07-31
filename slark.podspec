is_enable_https = true

Pod::Spec.new do |s|
    s.name         = "slark"

    s.version      = "0.0.1"

    s.summary      = "iOS and Android Video Player"

    s.ios.deployment_target = '16.5'

    s.description  = <<-DESC
        slark is a cross platform player that supports iOS and Android
    DESC
    s.homepage     = "https://github.com/Nevermore1994/slark"
   
    s.license      = { :type => "MIT", :file => "LICENSE" }

    s.author       = { "Nevermore-" => "875229565@qq.com" }

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
                    
                      'src/platform/iOS/*.h',
                      'src/platform/iOS/*.cpp',
                      'src/platform/iOS/*.mm',
                      'src/platform/iOS/interface/*.h',
                      'src/platform/iOS/interface/*.mm'

    s.public_header_files = 'src/platform/iOS/interface/*.h'  

    s.requires_arc = true
    
    s.pod_target_xcconfig = { 'ONLY_ACTIVE_ARCH' => 'YES' }

    s.user_target_xcconfig = { 
        'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'arm64'
    }

    if is_enable_https
        s.dependency "OpenSSL-Universal"
    end

    s.pod_target_xcconfig = { 
        'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'arm64',
        'HEADER_SEARCH_PATHS' => %(
            $(inherited)
            $(PODS_TARGET_SRCROOT)/include
            $(PODS_TARGET_SRCROOT)/libs/openssl/ios/include
        ).gsub("\n", " "),
        'USER_HEADER_SEARCH_PATHS' => %(
            $(PODS_TARGET_SRCROOT)
            $(PODS_TARGET_SRCROOT)/src/base
            $(PODS_TARGET_SRCROOT)/src/core
            $(PODS_TARGET_SRCROOT)/src/core/audio
            $(PODS_TARGET_SRCROOT)/src/core/codec
            $(PODS_TARGET_SRCROOT)/src/core/demuxer
            $(PODS_TARGET_SRCROOT)/src/http/include
            $(PODS_TARGET_SRCROOT)/src/http/include/public
            $(PODS_TARGET_SRCROOT)/src/platform/iOS
            $(PODS_TARGET_SRCROOT)/src/platform/iOS/interface
        ).gsub("\n", " "),
        'OTHER_LDFLAGS' => %(
            $(inherited)
            $(PODS_TARGET_SRCROOT)/libs/openssl/ios/libssl.a
            $(PODS_TARGET_SRCROOT)/libs/openssl/ios/libcrypto.a
        ).gsub("\n", " ")
    }

    enable_https_flag = is_enable_https ? 1 : 0

    s.xcconfig = { 
        'CLANG_C_LANGUAGE_STANDARD' => 'c17',
        'CLANG_CXX_LANGUAGE_STANDARD' => 'c++23',
        "GCC_PREPROCESSOR_DEFINITIONS" => "ENABLE_HTTPS=#{enable_https_flag} SLARK_IOS=1",
        'OTHER_CPLUSPLUSFLAGS' => '-Wall -Wextra -Wpedantic -Wcast-align -Wcast-qual -Wconversion -Wdisabled-optimization -Wendif-labels -Wfloat-equal -Winit-self -Winline -Wmissing-include-dirs -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wpacked -Wpointer-arith -Wredundant-decls -Wshadow -Wsign-promo -Wvariadic-macros -Wwrite-strings -Wno-variadic-macros -Wno-unknown-pragmas'
    }
    s.frameworks = 'Foundation', 'AVFoundation', 'VideoToolBox', 'AudioToolBox', 'CoreMedia'
  end

