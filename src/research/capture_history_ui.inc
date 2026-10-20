// Included inside lab_suite.cpp's UI namespace. No radio SPI or RF TX here.
struct HistoryUiSnapshot {
    bool active=false,thresholdReady=false,full=false;
    uint8_t count=0;
    int16_t thresholdDbm=0,rssiDbm=0;
    research::capture_history::Entry entries[4];
};
HistoryUiSnapshot historyUiSnapshot();
size_t captureHistorySelection;
uint8_t captureHistorySavedMask;
int8_t captureHistorySavedPathSlot=-1;
void playSavedCaptureHistory();

const char* captureHistorySaveErrorRu() {
    if(!strcmp(notice,"Radio still active"))return "Радио останавливается.\nПовторите запись.";
    if(!strcmp(notice,"No selected history capture"))return "Сначала примите сигнал.";
    if(!strcmp(notice,"History capture unavailable"))return "Сигнал недоступен.\nПовторите приём.";
    return "Ошибка записи на SD.";
}

bool saveSelectedCaptureHistory(size_t index) {
    const HistoryUiSnapshot snapshot=historyUiSnapshot();
    if(index>=snapshot.count||index>=4||!snapshot.entries[index].valid){showNotice("No selected history capture");return false;}
    if(!labStop()){showNotice("Radio still active");return false;}
    RegisterImage capturedRadio;RadioSettings capturedSettings;
    const size_t count=labCopyCaptureHistory(index,waveform,kWaveformCapacity,capturedRadio,capturedSettings);
    if(!count||count>kWaveformCapacity){showNotice("History capture unavailable");return false;}
    struct RestoreRecordingContext {
        RadioSettings savedSettings;RegisterImage savedRadio;
        RestoreRecordingContext():savedSettings(settings),savedRadio(radio){}
        ~RestoreRecordingContext(){settings=savedSettings;radio=savedRadio;}
    } restore;
    settings=capturedSettings;radio=capturedRadio;waveformCount=count;
    char path[112];
    if(!uniquePath(path,sizeof(path),"/subghz","LabRAW")||!saveWave(path)){showNotice("SD save failed");return false;}
    snprintf(captureSavedPath,sizeof(captureSavedPath),"%s",path);
    captureHistorySavedMask|=uint8_t(1U<<index);captureHistorySavedPathSlot=int8_t(index);showNotice(path);return true;
}

bool confirmCaptureHistoryExit() {
    resetInput();
    while(true) {
        input();
        if(holdBack){resetInput();return true;}
        if(ok||holdOk){resetInput();return false;}
        if(frame()){title("Не всё сохранено");line(2,"Выйти без записи?");footer("Держ.BACK:да OK:нет");display.display();}
    }
}

bool confirmCaptureHistoryRestart() {
    resetInput();
    while(true) {
        input();
        if(back||holdBack){resetInput();return false;}
        if(holdOk){resetInput();return true;}
        if(frame()) {
            title("Новый приём?");
            line(2,"История будет стёрта.");
            line(3,"До 4 новых сигналов.");
            footer("Держ.OK:да BACK:нет");display.display();
        }
    }
}

void captureHistoryApp() {
    if(settings.packetFormat!=PacketFormat::Async){message("Приём RAW","Выберите сигнал AM/FM\nв настройках приёма.");return;}
    struct RestoreTimeout {
        uint32_t saved;
        RestoreTimeout():saved(settings.rxTimeoutMs){settings.rxTimeoutMs=0;}
        ~RestoreTimeout(){settings.rxTimeoutMs=saved;}
    } restoreTimeout;
    if(!historyUiSnapshot().count&&!startMode(LabMode::Raw,true,true)){message("Приём RAW","Ошибка приёма: назад");return;}
    captureHistorySelection=0;captureHistorySavedMask=0;captureHistorySavedPathSlot=-1;resetInput();
    while(true) {
        input();
        const HistoryUiSnapshot snapshot=historyUiSnapshot();
        const size_t count=snapshot.count>4?4:snapshot.count;
        if(holdBack)break;
        if(back) {
            const uint8_t validMask=count?uint8_t((1U<<count)-1):0;
            if(!(validMask&~captureHistorySavedMask)||confirmCaptureHistoryExit())break;
            continue;
        }
        if(!count)captureHistorySelection=0;
        else if(captureHistorySelection>=count)captureHistorySelection=count-1;
        if(up&&count)captureHistorySelection=(captureHistorySelection+count-1)%count;
        if(down&&count)captureHistorySelection=(captureHistorySelection+1)%count;
        if(serialCapturePlayPending){serialCapturePlayPending=false;playSavedCaptureHistory();continue;}
        if(holdOk) {
            if(confirmCaptureHistoryRestart()) {
                if(!labStop())showNotice("Radio still active");
                else if(!labClearCaptureHistory())message("Приём RAW","История занята.\nПовторите очистку.");
                else if(!startMode(LabMode::Raw,true,true))message("Приём RAW","Ошибка приёма: назад");
                else {captureHistorySelection=0;captureHistorySavedMask=0;captureHistorySavedPathSlot=-1;}
            }
            resetInput();continue;
        }
        if(ok||serialCaptureSavePending) {
            const bool serialRequest=serialCaptureContext&&serialCaptureSavePending;
            serialCaptureSavePending=false;
            const bool saved=saveSelectedCaptureHistory(captureHistorySelection);
            if(serialRequest) {
                if(saved)Serial.printf("[Capture] SAVED path=%s pulses=%lu slot=%u\n",captureSavedPath,(unsigned long)waveformCount,unsigned(captureHistorySelection+1));
                else Serial.printf("[Capture] ERROR save=%s\n",notice);
            } else if(saved)message("Сигнал сохранён",captureSavedPath);
            else if(count)message("Запись на SD",captureHistorySaveErrorRu());
            resetInput();continue;
        }
        if(frame()) {
            char text[80];snprintf(text,sizeof(text),"Приём: %u/4",unsigned(count));title(text);
            for(size_t i=0;i<4;++i) {
                if(i<count) {
                    const auto& entry=snapshot.entries[i];
                    snprintf(text,sizeof(text),"%u %luимп %lluмс",unsigned(i+1),(unsigned long)entry.count,(unsigned long long)(entry.durationUs/1000));
                } else snprintf(text,sizeof(text),"%u --",unsigned(i+1));
                const bool selected=count&&i==captureHistorySelection;
                if(selected)display.fillRect(0,13+i*8,128,8,SH110X_WHITE);
                ui_text::draw(display,1,13+i*8,text,selected?SH110X_BLACK:SH110X_WHITE,21);
            }
            const char* state=snapshot.full?"4/4. Держ.OK: новый":!snapshot.active?"Пауза. OK: запись":!snapshot.thresholdReady?"Настройка по шуму...":"Слушаю. OK: записать";
            ui_text::draw(display,0,46,state);
            footer("UP/DN OK:SD BACK");display.display();
        }
    }
    while(!labStop()){showNotice("Waiting for radio worker to stop");delay(10);}
    if(!labClearCaptureHistory())showNotice("History clear failed");
    resetInput();
}
