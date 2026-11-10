# Продаж без комерційної ліцензії Qt (LGPLv3)

Qt Core / Gui / Widgets / Network / Bluetooth доступні під LGPLv3, тож закриту програму можна
продавати за таких умов:

1. **Тільки динамічна лінковка.** Qt — окремі DLL поряд з exe (`cmake --install` / `windeployqt`).
2. **Без модулів, які в open-source Qt тільки під GPL:** Qt Charts, Data Visualization / Graphs,
   Virtual Keyboard, Quick 3D, Lottie, CoAP / MQTT / OPC UA, NetworkAuth тощо. Перед новим модулем —
   https://doc.qt.io/qt-6/qtmodules.html.
3. **Тексти ліцензій** у `licenses/` (`LGPL-3.0.txt`, `GPL-3.0.txt`, є в `C:/Qt/Licenses`) — CMake
   копіює їх у `dist/licenses`.
4. **Повідомити користувача:** `THIRD_PARTY_NOTICES.txt` (версія Qt, контакт) + «Про Qt…» у налаштуваннях.
5. **Доступ до коду Qt** тієї версії, що постачається (посилання на download.qt.io або пропозиція
   надіслати). Зміни в самому Qt теж публікувати. Свій код відкривати не треба.
6. **EULA не забороняє** заміну Qt DLL і реверс-інжиніринг у межах, потрібних для цього.
7. Продукт не називати так, ніби він від Qt.

Не юридична консультація: https://www.qt.io/licensing/open-source-lgpl-obligations.
