EnglishTeacher(产品)是一个基于eps32s3芯片开发的便携墨水屏设备
其终端系统(本repo),folk自78/xiaozhi-esp32,并在78/xiaozhi-esp32项目上做了如下修改

修改.gitignore
    注释components,以便导入外部组件
    注释sdkconfig,以便持久化sdk配置

修改sdkconfig
    首先修改device target为esp32s3,等待esp-idf自动修改sdkconfig,然后修改参数CONFIG_FREERTOS_HZ=1000,

新增 main/boards/EnglishTeacher
    增加boards配置

修改 main/Kconfig.projbuild
    设置BOARD_TYPE为EnglishTeacher

新增 components/EnglishTeacher
    EnglishTeacher核心组件,将不依赖原main组件接口的内容保存到这里.

新增 components/Adafruit_BusIO
    在原repo基础上增加idf_component.yml

新增 components/Adafruit-GFX
    在原repo基础上增加idf_component.yml

新增 components/esp32_arduino_sqlite3_lib
    在原repo基础上增加idf_component.yml

新增 components/GxEPD2
    在原repo基础上增加idf_component.yml

新增 components/SdFat
    在原repo基础上增加idf_component.yml

新增 main/etacher
    在main组件中增加的内容,用于调用component中的组件和原main中的接口