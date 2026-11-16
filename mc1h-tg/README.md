# mc1h-tg

Личный Telegram-клиент на TDLib (JSON-интерфейс). Это не форк AyuGram: код написан с нуля.

- TDLib: 1.8.70, commit `03220f6b3043c6fd7785fbf0901d63f19ad19974`, submodule `third_party/td`.
- JSON: nlohmann/json 3.11.3 (MIT), один заголовок в `third_party/nlohmann`, SHA-256 `9bea4c80…103ea6`.
- Ядро (`src/core`) не зависит ни от UI, ни от TDLib. TDLib подключается только в `src/app`.

## Состав

| Файл | Назначение |
|---|---|
| `src/core/transport.h` | `ITransport`: send / receive / execute |
| `src/core/ghost_policy.*` | правила ghost mode для аккаунта и для отдельных чатов |
| `src/core/ghost_client.*` | обёртка над транспортом: заблокированный запрос не отправляется и получает синтетический `ok` с тем же `@extra` |
| `src/core/update_router.*` | маршрутизация updates по `@type` и ответов по `@extra` |
| `src/core/archive.*` | SQLite + FTS5: сообщения, история правок, отметка удаления |
| `src/core/archive_recorder.*` | связывает updates и ответы `getChatHistory` с архивом |
| `src/core/auth_flow.*` | машина состояний авторизации (телефон, код, 2FA, регистрация, email) без ввода-вывода |
| `src/core/metadata_strip.*` | очистка метаданных JPEG/PNG без перекодирования пикселей |
| `src/app/td_json_transport.*` | `ITransport` поверх `td_json_client.h` |
| `src/app/console.*`, `cli_main.cpp` | консольный клиент |
| `tools/mc1h_strip.cpp` | CLI для очистки метаданных |
| `tests/test_main.cpp` | модульные тесты |
| `tests/td_smoke.cpp` | smoke-тест против настоящего `libtdjson`, аккаунт не нужен |
| `tests/e2e_strip.py` | сквозная проверка `mc1h_strip` на файлах Pillow |

### Что блокирует ghost mode

Имена методов сверены с `td_api.tl` из 1.8.70.

| Функция | Методы |
|---|---|
| `reads` | `viewMessages`, `readAllChatMentions`, `readAllChatReactions`, `readAllChatPollVotes`, `readAllForumTopic{Mentions,Reactions,PollVotes}`, `readAllDirectMessagesChatTopicReactions`, `readBusinessMessage`, `readChatList` |
| `typing` | `sendChatAction` |
| `online` | `setOption("online", optionValueBoolean true)` |
| `stories` | `openStory` |
| `opens` | `openMessageContent` |
| `ads` | `viewSponsoredChat`, `viewVideoMessageAdvertisement` |

Для `reads`, `typing`, `stories` и `opens` можно задать переопределение на уровне чата. Для `stories` ключом служит `story_poster_chat_id`.

## Сборка: Linux

Нужны gcc ≥ 13 или clang, cmake ≥ 3.21, ninja, gperf, а также dev-пакеты libssl, zlib и libsqlite3 с FTS5.

```sh
git submodule update --init --depth 1 mc1h-tg/third_party/td
# TDLib (один раз, ~30 мин на 4 ядрах)
cmake -S mc1h-tg/third_party/td -B td-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PWD/td-install
cmake --build td-build --target install
# mc1h
cmake -S mc1h-tg -B mc1h-tg/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMC1H_WITH_TDLIB=ON -DCMAKE_PREFIX_PATH=$PWD/td-install
cmake --build mc1h-tg/build && (cd mc1h-tg/build && ctest --output-on-failure)
python3 -I mc1h-tg/tests/e2e_strip.py mc1h-tg/build/mc1h_strip /tmp/mc1h-e2e
```

Без `-DMC1H_WITH_TDLIB=ON` собираются только ядро, `mc1h_strip` и тесты. Для сборки с санитайзерами есть флаг `-DMC1H_SANITIZE=ON`. Под LeakSanitizer нужно задать `LSAN_OPTIONS=suppressions=tests/lsan.supp`: встроенный в TDLib SQLite оставляет одноразовые аллокации.

## Сборка: Windows / MSVC (НЕ проверено)

Предполагаемый путь, на этой машине не запускался:

```bat
vcpkg install openssl zlib gperf sqlite3[fts5] --triplet x64-windows
cmake -S mc1h-tg\third_party\td -B td-build -A x64 -DCMAKE_TOOLCHAIN_FILE=<vcpkg>\scripts\buildsystems\vcpkg.cmake -DCMAKE_INSTALL_PREFIX=%CD%\td-install
cmake --build td-build --config Release --target install
cmake -S mc1h-tg -B mc1h-tg\build -A x64 -DMC1H_WITH_TDLIB=ON -DCMAKE_PREFIX_PATH=%CD%\td-install -DCMAKE_TOOLCHAIN_FILE=<vcpkg>\scripts\buildsystems\vcpkg.cmake
cmake --build mc1h-tg\build --config Release
```

## Запуск

Секреты берутся только из окружения:

- `MC1H_API_ID`, `MC1H_API_HASH` — обязательны, выдаются на my.telegram.org;
- `MC1H_DB_KEY` — ключ шифрования базы TDLib. Без него база не шифруется, и клиент об этом предупреждает.

```sh
MC1H_API_ID=… MC1H_API_HASH=… MC1H_DB_KEY=… ./mc1h_cli --data-dir ~/.mc1h [--test-dc] [--verbosity 1]
```

В `--data-dir` лежат: `tdlib/` (база TDLib), `files/`, `archive.sqlite` (локальный архив, пока **без шифрования**), `ghost.json` и `tdlib.log`. Список команд выводит `help`. Во время авторизации на любой запрос можно ответить `/quit`.
