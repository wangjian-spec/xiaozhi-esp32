#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <memory>


#include "eteacher/app_service/app_service.h"
#include "system_info.h"
#include "board.h"
#include "display.h"
#include "eteacher/app_manager/app_manager.h"
#include "eteacher/apps/free_conversation/free_conversation.h"
#include "eteacher/apps/device_setting/device_setting.h"
#include "eteacher/apps/scene_conversation/scene_conversation.h"
#include "eteacher/apps/word_practice/word_practice.h"
#include "eteacher/apps/calendar_schedule/calendar_schedule.h"
#include "eteacher/apps/sd_file_mgr/sd_file_mgr.h"
//#include "eteacher/apps/dictionary/dictionary.h"
#include "eteacher/apps/words_book/words_book.h"
#include "eteacher/apps/daily_reading/daily_reading.h"
#include "eteacher/apps/mission_game/mission_game.h"
#include "eteacher/apps/txt_reader/txt_reader.h"
#include "eteacher/apps/words_game/words_game.h"


#define TAG "main"

extern "C" void app_main(void)
{
    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize Board singleton early so AppManager can render menu
    auto& board = Board::GetInstance();

    // Initialize AppManager and register demo apps
    auto& app_mgr = AppManager::GetInstance();
    app_mgr.Init(board);
    
    app_mgr.Register(MakeWordPracticeApp());
    app_mgr.Register(MakeSceneConversationApp());
    app_mgr.Register(MakeFreeConversationApp());

    app_mgr.Register(MakeCalendarScheduleApp());
 
    app_mgr.Register(MakeDailyReadingApp());

    app_mgr.Register(MakeTxtReaderApp());
    app_mgr.Register(MakeDeviceSettingApp());
    app_mgr.Register(MakeMissionGameApp());
    app_mgr.Register(MakeWordsGameApp());
    //app_mgr.Register(MakeDictionaryApp());
    app_mgr.Register(MakeWordsBookApp());
    app_mgr.Register(MakeSdFileMgrApp());
    app_mgr.FinalizeRegistration();

    // Initialize and run the application
    // auto& app = Application::GetInstance();
    // app.Initialize();
    // app.Run();  // This function runs the main event loop and never returns

     auto& app = AppService::GetInstance();
     app.Initialize();
     app.Run();  // This function runs the main event loop and never returns

}
