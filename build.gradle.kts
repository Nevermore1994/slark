plugins {
    alias(libs.plugins.android.library) apply false
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.kotlin.android) apply false
    alias(libs.plugins.kotlin.compose) apply false
    idea
}

// 根项目需要一个实际的 task，否则 Android Studio 可能不会为其生成模块配置
tasks.register("dummyRootTask") {
    description = "Ensure root project is recognized as a valid module"
    group = "verification"
}

// 配置额外的源码目录，让 IDE 能索引到核心 C++ 代码
idea.module.apply {
    // 使用 set 替代 plusAssign，避免重复
    val extraSourceDirs = setOf(
        file("src"),
        file("include"),
        file("internal")
    ) + file("src").walkTopDown().filter { it.isDirectory }.toSet() +
      file("include").walkTopDown().filter { it.isDirectory }.toSet() +
      file("internal").walkTopDown().filter { it.isDirectory }.toSet()
    sourceDirs = sourceDirs + extraSourceDirs
    excludeDirs = excludeDirs + file("build")
}
