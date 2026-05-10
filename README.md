# MicGainControl

MicGainControl is a lightweight Windows utility that monitors and enforces a specific microphone volume level. It prevents other applications (like Discord, Skype, or Zoom) from automatically adjusting your microphone gain.

## Features
- **Real-time enforcement**: Instantly reverts any external volume changes.
- **Auto-start**: Automatically starts with Windows.
- **Configurable**: Easily change the target volume level.
- **Lightweight**: Minimal CPU and memory footprint using event-driven Core Audio API.
- **Hot-reload**: Configuration changes are applied immediately without restarting.

---

# MicGainControl (Русский)

MicGainControl — это легкая утилита для Windows, которая отслеживает и принудительно поддерживает заданный уровень громкости микрофона. Она предотвращает автоматическую регулировку усиления микрофона другими приложениями (такими как Discord, Skype или Zoom).

## Особенности
- **Принуждение в реальном времени**: Мгновенно возвращает громкость при попытках внешнего изменения.
- **Автозагрузка**: Автоматически запускается вместе с Windows.
- **Настройка**: Легко изменить целевой уровень громкости.
- **Легкость**: Минимальное потребление ресурсов благодаря событийно-ориентированному Core Audio API.
- **Горячая перезагрузка**: Изменения конфигурации применяются мгновенно без перезапуска.

---

## How it Works / Как это работает

**English:**
The application uses the Windows Core Audio API (`IAudioEndpointVolumeCallback`) to listen for volume change events. When a change is detected from an external source, MicGainControl compares it with the target volume and sets it back if they differ. It also listens for default device changes to ensure the active microphone is always controlled.

**Русский:**
Приложение использует Windows Core Audio API (`IAudioEndpointVolumeCallback`) для прослушивания событий изменения громкости. Когда обнаруживается изменение из внешнего источника, MicGainControl сравнивает его с целевым значением и возвращает обратно, если они различаются. Программа также отслеживает смену устройства по умолчанию, чтобы активный микрофон всегда оставался под контролем.

## Installation / Установка

**English:**
1. Download `MicGainControl.exe` from the [latest release](https://github.com/user/MicGainControl/releases/latest).
2. Place the executable in a permanent folder (e.g., `%APPDATA%\MicGainControl`).
3. Run the application. It will automatically add itself to the Windows startup registry.

**Русский:**
1. Скачайте `MicGainControl.exe` из [последнего релиза](https://github.com/user/MicGainControl/releases/latest).
2. Поместите исполняемый файл в постоянную папку (например, `%APPDATA%\MicGainControl`).
3. Запустите приложение. Оно автоматически добавит себя в реестр автозагрузки Windows.

## Configuration / Настройка

**English:**
The configuration file is located at `%USERPROFILE%\.MicGainControl.json`. You can open it via the tray icon context menu ("Open Config").
```json
{
  "microphoneVolume": 100,
  "enabled": true
}
```
- `microphoneVolume`: Target volume level (0 to 100).
- `enabled`: Set to `false` to temporarily stop enforcement.

**Русский:**
Файл конфигурации находится по пути `%USERPROFILE%\.MicGainControl.json`. Вы можете открыть его через контекстное меню иконки в трее ("Open Config").
```json
{
  "microphoneVolume": 100,
  "enabled": true
}
```
- `microphoneVolume`: Целевой уровень громкости (от 0 до 100).
- `enabled`: Установите `false`, чтобы временно отключить принудительную установку громкости.

## FAQ / Часто задаваемые вопросы

**Q: Can I delete the file after starting?**
**A:** No. The program runs from this file, and the autostart entry points to its current location.

**Q: How do I update?**
**A:** Exit the program, replace the `.exe` with the new version, and start it again.

**В: Можно ли удалить файл после запуска?**
**О:** Нет. Программа работает из этого файла, и запись в автозагрузке указывает на его текущее местоположение.

**В: Как обновить программу?**
**О:** Выйдите из программы, замените `.exe` файл новой версией и запустите её снова.

## Building / Сборка

Requires CMake and Visual Studio (MSVC).
```bash
cmake -B build
cmake --build build --config Release
```
