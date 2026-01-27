中英文词典 App
1.中英文词典可以通过输入英文查找对应中文，也可以通过输入中文查找对应英文
2.通过软键盘输入文字，或者通过ASR识别

数据库表名称为ecdict，按start进入到该app中，便可以通过word字段搜索，获取并显示phonetic字段为音标，definition字段为单词释义， translation 字段为单词释义
内容通过墨水屏显示出来，
在dictionary.cc app中帮我编写代码，在main.cc中注册该app，不修改其它任何代码，帮我实现通过SD卡上的resource\database\frq_bnc_tag.db 数据库数据源，数据库表名称为ecdict，可以通过word字段搜索，获取并显示phonetic字段为音标，definition字段为单词释义， translation 字段为单词释义；start按键进入app，select按键退回；
按住A可以通过语音输入文字ASR，C键调出软键盘




词典界面布局设计
如果查询成功，可以通过按键或选项将单词添加到生词本