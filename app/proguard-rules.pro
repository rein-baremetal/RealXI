# JNI entry points are looked up by name from C++.
-keep class com.realxi.engine.NativeBindings { *; }
-keepclasseswithmembernames class * { native <methods>; }
