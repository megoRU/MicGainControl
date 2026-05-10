# MicGainControl

Лёгкая Win32 утилита для Windows, которая принудительно удерживает заданную громкость микрофона.

## Возможности

- Работа в трее
- Автозапуск
- Мгновенное применение настроек
- Минимальное потребление ресурсов

## Конфиг

```json
{
  "enabled": true,
  "microphoneVolume": 100
}
````

## Сборка

```bash
cmake -B build
cmake --build build --config Release
```

## Технологии

* C++17
* Win32 API
* WASAPI
