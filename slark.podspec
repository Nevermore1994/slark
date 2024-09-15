Pod::Spec.new do |s|
    s.name         = "slark"

    s.version      = "0.0.1"

    s.summary      = "iOS and Android Video Player"

    s.ios.deployment_target = '13.0'

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
                      'src/third_party/**/*.h'

    s.public_header_files = 'slark/**/*.hpp',
                            'slark/**/*.h'

    s.requires_arc = true
    
    s.pod_target_xcconfig = { 'ONLY_ACTIVE_ARCH' => 'YES' }
    
    s.pod_target_xcconfig = { 
        'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'arm64',
        'HEADER_SEARCH_PATHS' => '"$(inherited)" "$(PODS_TARGET_SRCROOT)/include"',
        'USER_HEADER_SEARCH_PATHS' => '"$(PODS_TARGET_SRCROOT)"'\
            '"$(PODS_TARGET_SRCROOT)/base"' \
            '"$(PODS_TARGET_SRCROOT)/base/platform/iOS"' \
            '"$(PODS_TARGET_SRCROOT)/core"' \
            '"$(PODS_TARGET_SRCROOT)/core/audio"' \
            '"$(PODS_TARGET_SRCROOT)/core/codec"' \
            '"$(PODS_TARGET_SRCROOT)/core/muxer"' \
            '"$(PODS_TARGET_SRCROOT)/core/platform/iOS"'
    }

    s.user_target_xcconfig = { 
        'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'arm64'
    }

    s.xcconfig = { 
        'CLANG_C_LANGUAGE_STANDARD' => 'c17',
        'CLANG_CXX_LANGUAGE_STANDARD' => 'c++23',
        "GCC_PREPROCESSOR_DEFINITIONS" => 'SLARK_IOS=1',
        'CLANG_CXX_LIBRARY' => 'libc++',
        'OTHER_CPLUSPLUSFLAGS' => '-Wall -Wextra -Wpedantic -Wcast-align -Wcast-qual -Wconversion -Wdisabled-optimization -Wendif-labels -Wfloat-equal -Winit-self -Winline -Wmissing-include-dirs -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wpacked -Wpointer-arith -Wredundant-decls -Wshadow -Wsign-promo -Wvariadic-macros -Wwrite-strings -Wno-variadic-macros ',
        'OTHER_LDFLAGS' => '-fsanitize=address'
    }
    s.frameworks = 'Foundation', 'AVFoundation', 'VideoToolBox', 'AudioToolBox'
  end

