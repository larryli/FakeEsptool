烧录器端参考代码库：

1、相对项目根目录 ../esptool/esptool/ 对应 https://github.com/espressif/esptool 代码库

2、相对项目根目录 ../esptool/esptool-js/ 对应 https://github.com/espressif/esptool-js 代码库

3、相对项目根目录 ../esptool/web-esptool/ 对应 https://github.com/xingrz/web-esptool 代码库

协议解码器参考代码库：

1、相对项目根目录 ../esptool/sigrok-esp32-programmer-decoder/ 对应 https://github.com/ruediste/sigrok-esp32-programmer-decoder 代码库

注意：

1、请先配置好开发环境，了解烧录器端、协议解码器代码和本项目文档。

2、完成 TODO 待办之后，从该文档中清理具体项目。然后同步到其他文档（需求、开发、协议文档）。空的 TODO 文档也需要保留。

3、代码修改完成后自动构建验证修改。修改无误，结束对话前自动运行最后构建的程序。

4、除了 esptool/esptool-js 官方烧录器代码实现，其他代码不一定准确。

构建环境：

本项目使用 MSVC + CMake 构建。在 PowerShell 中通过 `Microsoft.VisualStudio.DevShell.dll` 初始化环境。

首选开发环境为 Visual Studio Build Tools 2026 `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\`。

构建产物：build\FakeEsptool.exe
