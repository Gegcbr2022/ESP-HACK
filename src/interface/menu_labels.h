#pragma once
#include <stddef.h>
#include <string.h>

// Exact UI labels only. Stored settings, file names and protocol data are never
// rewritten; unknown technical labels retain their original spelling.
namespace ui_menu {
struct Label { const char* source; const char* russian; };
inline const char* label(const char* source) {
  static const Label labels[] = {
    {"Settings", "Настройки"}, {"Interface", "Интерфейс"},
    {"Restart", "Перезапуск"}, {"Reset", "Сброс настроек"},
    {"Update", "Обновление"}, {"About", "О программе"},
    {"Menu", "Главное меню"}, {"Submenu", "Подменю"},
    {"Color", "Цвет экрана"}, {"Standby", "Таймер экрана"},
    {"Boot Logo", "Логотип запуска"}, {"Pages", "Страницы"},
    {"List", "Список"}, {"White", "Белый"}, {"Black", "Чёрный"},
    {"Disable", "Выключено"}, {"15s", "15 сек"}, {"30s", "30 сек"},
    {"1m", "1 мин"}, {"3m", "3 мин"}, {"5m", "5 мин"},
    {"10m", "10 мин"}, {"30m", "30 мин"},
    {"Back", "Назад"}, {"Apps", "Приложения"}, {"SD Apps", "Приложения SD"},
    {"Read", "Чтение"}, {"Read RAW", "Запись RAW"},
    {"Send", "Передача файла"}, {"Analyzer", "Анализатор"},
    {"Radio Lab", "Радио"}, {"Chat", "Чат"}
  };
  if (!source) return "";
  for (size_t i = 0; i < sizeof labels / sizeof labels[0]; ++i)
    if (!strcmp(source, labels[i].source)) return labels[i].russian;
  return source;
}
}
