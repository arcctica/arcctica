# mc1h_STATE — рабочее состояние mc1h-tg

Обновлено: 2026-11-16. Ветка `claude/telegram-client-tdlib-t8fqqo`, репозиторий `arcctica/arcctica` (публичный), папка `mc1h-tg/`.

## Контекст
- Кода прежней сессии (ghost_policy, archive, …, «99/99») в репозитории, его истории и контейнере не нашлось. По решению пользователя ядро переписано заново.
- TDLib 1.8.70 @ 03220f6b подключён как submodule. Сборка и установка шли в `/home/user/td-build` и `/home/user/td-install`, вне репозитория; эти каталоги эфемерны.

## Проверено (Linux, g++ 13.3, clang, cmake 3.28)
- `mc1h_tests`: 234/234 под ASan/UBSan, Release и clang (`-Werror -Wshadow -Wconversion`). Мутационная проверка: 3 из 3 мутантов пойманы (ghost не блокирует, COM не удаляется, удаление из кэша считается удалением).
- `e2e_strip.py`: 154/154 на Pillow 12.3.0. JPEG с ориентацией 1/3/6/8, прогрессивный JPEG, JPEG без ICC, PNG RGB и RGBA. Пиксели идентичны, `exif_transpose` даёт тот же результат, EXIF содержит только Orientation, GPS/Make/Model/Date/XMP/COM/tEXt/iTXt/zTXt удалены, ICC и pHYs сохранены. Также проверены флаги `--drop-*` и отказ на GIF и усечённом JPEG.
- `mc1h_td_smoke` на настоящем libtdjson 1.8.70: заблокированный `viewMessages` не уходит и получает синтетический `ok`; разблокированный уходит и получает ответ TDLib `400 Initialization parameters are needed` с тем же `@extra`; `td_execute(getTextEntities)` работает; `close` приводит к `authorizationStateClosed`.
- `mc1h_cli` с фиктивными api_id/api_hash на test DC: `getOption version` → `WaitTdlibParameters` → `setTdlibParameters` → запрос телефона → `/quit` → `Closed`. Тот же `MC1H_DB_KEY` повторно открывает базу, неверный ключ даёт `401 Wrong database encryption key`, затем чистое закрытие с exit 1.

## Проверено по исходникам TDLib (на практике НЕ проверено)
- `StoryManager::open_story` (StoryManager.cpp:3374-3388): для активной истории вызывает `read_stories_on_server` (`stories.readStories`), для закреплённой неактивной — `increment_story_views` (`stories.incrementStoryViews`). Блокировка `openStory` перекрывает оба пути. Других источников `stories.readStories`, кроме повтора log event (6988), нет.
- Самоуничтожающееся медиа: `get_message_content_object` для Photo (MessageContent.cpp:11924-11933) отдаёт полный объект `photo` с file_id при любом `is_content_secret`. Отметку прочтения ставит только `open_message_content` (MessagesManager.cpp:17236-17239, `read_message_contents_on_server`). Значит, по исходникам `downloadFile` до `openMessageContent` должен работать.
- Отдельно: при открытом чате `view_messages` вызывает `view_story_message` (MessagesManager.cpp:17031-17034). Это только перезагрузка истории, не просмотр.

## НЕ сделано / НЕ проверено
- Вход в настоящий или тестовый аккаунт: в сессии нет api_id/api_hash. Нужен запуск у пользователя.
- Сборка под Windows/MSVC не пробовалась (только clang/gcc на Linux).
- Qt UI отсутствует. SQLCipher не подключён: `archive.sqlite` не шифруется.
- Онлайн при отправке сообщения сервер выставляет сам. Клиентская блокировка `setOption` от этого не защищает (поведение сервера, не проверено).
- Архив хранит при правке старый текст и тип, но не старый `raw_json` (file_id прежнего медиа теряется).
- Очистка метаданных проверена на файлах Pillow, но не на снимках реальных камер (MakerNote, MPF с вложенными превью). MPF и данные после EOI покрыты синтетическим модульным тестом.

## Следующая проверка
1. Пользователь: `MC1H_API_ID/HASH` → `mc1h_cli --test-dc` с тестовым номером 99966XYYYY (код из документации Telegram для test DC, не проверено) → `chats`, `history`, `send`, `ghost reads on` + `read`, `show`.
2. Затем каркас Qt UI.
