# MicGainControl

Современное приложение для Windows на базе WinUI 3, которое управляет и при необходимости принудительно удерживает заданную громкость микрофона.

## Возможности

- Современный пользовательский интерфейс WinUI 3 (C++/WinRT + Windows App SDK + XAML)
- Автоматическая поддержка светлой и тёмной тем оформления Windows
- Регулировка громкости микрофона ползунком без необходимости нажимать «Сохранить»
- Мгновенное применение настроек
- Работа в системном трее с контекстным меню («Включено», «Открыть окно», «Выход»)
- Автозапуск (через реестр Windows)
- Минимальное потребление системных ресурсов

## Запуск

1. Переместите `MicGainControl.exe` в постоянную папку, например:

```txt
C:\Program Files\MicGainControl\
```

2. Откройте свойства файла → поставьте галочку `Разблокировать` → `Применить`.

3. Запустите `MicGainControl.exe`. Приложение запустится и добавит иконку в системный трей.

## Сборка проекта локально

Для локальной сборки проекта понадобятся:
- Visual Studio 2022 с установленным компонентом «Разработка приложений для C++» и поддержкой C++/WinRT.
- CMake (3.15 или выше) и NuGet CLI (`nuget.exe`).

### Шаги для сборки:

1. Восстановите NuGet пакеты:
```cmd
nuget restore packages.config -OutputDirectory packages
```

2. Сконфигурируйте проект с помощью CMake:
```cmd
cmake -B build -A x64 -DCMAKE_BUILD_TYPE=Release
```

3. Соберите проект:
```cmd
cmake --build build --config Release
```

Исполняемый файл будет находиться в директории `build/Release/MicGainControl.exe`.

## Автозапуск

Программа автоматически прописывает себя в автозагрузку при каждом запуске через реестр Windows по пути:
`HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run` (параметр `MicGainControl`).

## Конфиг

Файл настроек `.MicGainControl.json` создается автоматически в домашней папке пользователя:
`%USERPROFILE%\.MicGainControl.json`.

```json
{
  "enabled": true,
  "microphoneVolume": 100
}
```

## Технологии

* C++17 / C++/WinRT
* WinUI 3 / Windows App SDK
* XAML
* WASAPI
* Win32 Shell API
