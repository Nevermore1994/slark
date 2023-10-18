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

    s.author             = { "Nevermore-" => "875229565@qq.com" }

    s.source       = { :git => "https://github.com/Nevermore1994/slark.git", :tag => "#{s.version}" }

    s.source_files  = "slark/slark/**/*.{h, hpp, mm, cpp}"

    s.public_header_files = "slark/slark/**/*.{h, hpp}"

    s.requires_arc = true
    
    s.pod_target_xcconfig = { 'ONLY_ACTIVE_ARCH' => 'YES' }
    
    s.pod_target_xcconfig = { 'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'arm64' }

    s.user_target_xcconfig = { 'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'arm64' }
    
    s.xcconfig = { 'CLANG_CXX_LANGUAGE_STANDARD' => 'c++2b' }

    s.frameworks = 'Foundation', 'AVFoundation', 'VideoToolBox', 'AudioToolBox'
  end