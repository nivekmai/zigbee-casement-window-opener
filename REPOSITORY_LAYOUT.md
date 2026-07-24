# Repository layout

```text
.
├── .github/workflows/
│   ├── build.yml          # ESP-IDF compile and downloadable artifacts
│   ├── release.yml        # Tagged GitHub Releases
│   └── test.yml           # Host-side state-machine tests
├── main/
│   ├── app_main.c
│   ├── encoder.c/.h
│   ├── pins.h
│   ├── window_controller.c/.h
│   └── zigbee_window.c/.h
├── tests/
│   ├── requirements.txt
│   └── test_state_machine.py
├── CMakeLists.txt
├── idf_component.yml
├── partitions.csv
├── sdkconfig.defaults
├── wiring.svg
└── README.md
```
