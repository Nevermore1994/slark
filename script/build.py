import os
import argparse

now_path = os.getcwd()
root_path = os.path.abspath(os.path.dirname(now_path))
build_path = os.path.join(root_path, "cmake-build")


def delete_cmake_cache():
    print("-------clear start -------\n")
    command = "make clean; rm -rf ../cmake-build-debug ../cmake-build-release"
    print(command)
    os.system(command)
    print("-------clear end -------\n")


def gen_project(args):
    print("-------gen start -------\n")
    global build_path
    build_path += "-" + args.type.lower()
    if args.platform == "iOS":
        command = "cmake .. -B {}/iOS -G Xcode " \
                  "-DCMAKE_TOOLCHAIN_FILE=../../ios.toolchain.cmake " \
                  "-DBUILD_PLATFORM=iOS" \
                  "-DPLATFORM=OS64 " \
                  "-DENABLE_BITCODE=FALSE " \
                  "-DDEPLOYMENT_TARGET=16.5 ".format(build_path)
        print(command)
        os.system(command)
        if args.demo:
            command = "cd ../demo/iOS/demo && pod install --repo-update"
            print(command)
            os.system(command)
            command = "cd ../demo/iOS/demo && open demo.xcworkspace"
        else:
            command = "open {}/iOS/slark.xcodeproj".format(build_path)

    elif args.platform == "Android":
        command = "cmake .. -B {}/Android -G 'Ninja' " \
                  "-DCMAKE_SYSTEM_NAME=Android " \
                  "-DBUILD_PLATFORM=Android" \
                  "-DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake " \
                  "-DANDROID_ABI=arm64-v8a " \
                 "-DCMAKE_BUILD_TYPE={}" \
                  "-DANDROID_PLATFORM=29 ".format(build_path, args.type)
        print(command)
        os.system(command)
        if args.demo:
            command = "open -a 'Android Studio' '../demo/Android' "
        else:
            command = "open -a 'Android Studio' '../src/platform/Android/' "
    else:
        command = "cmake -DCMAKE_MAKE_PROGRAM=/usr/bin/make -S {} -B {} -G 'Unix Makefiles'" \
            .format(root_path, build_path)
    print(command)
    os.system(command)
    print("-------gen end -------\n")


def parse_args():
    global build_path
    parser = argparse.ArgumentParser(description="slark build script")
    parser.add_argument('-p', '--platform', action='store', help="build platform", choices=["iOS", "Android"])
    parser.add_argument('-t', '--type', action='store', help="build type", default="Debug",
                        choices=["Debug", "Release"])
    parser.add_argument('--disable_http', action='store_true', help="disable http")
    parser.add_argument('--disable_test', action='store_true', help="disable test")
    parser.add_argument('--demo', action='store_true', help="build demo")
    args = parser.parse_args()
    delete_cmake_cache()
    gen_project(parser.parse_args())

def main():
    parse_args()

if __name__ == '__main__':
    main()
