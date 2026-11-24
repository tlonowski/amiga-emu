# AmigaEmu — TODO

## Zgodność sprzętowa A500 / OCS
- [x] **Emulacja *cycle-exact* magistrali i DMA:** podział wykonania instrukcji na cykle szyny oraz spowalnianie 68000 przez kradzież cykli DMA (bitplany, Copper, Blitter, Audio, Disk) — kluczowe dla wymagających dem scenowych i trików z wiązką w połowie linii.
- [x] **Cyklotaktowany Blitter:** rozłożenie pracy Blittera na kolejne cykle DMA zamiast natychmiastowego wykonywania w momencie zapisu `BLTSIZE`.
- [x] **Zegar RTC (A501 / A4000):** emulacja zegara czasu rzeczywistego pod `$DC0000` (Ricoh RP5C01 / MSM6242B).
- [x] **Port szeregowy i równoległy:** podstawowa obsługa `SERDAT`/`SERPER`/`SERDATR` oraz portu równoległego Centronics (`CIA-A PRB` + handshake `BUSY`/`POUT`/`SEL`/`/ACK` na `CIA-B PRA` i `CIA-A FLAG`).
- [x] **Wydruk z Amigi na drukarkę macOS (wszystkie modele: A500 / A600 / A1200 / A4000):** przechwytywanie strumienia z portu równoległego i szeregowego (`PRT:` / `PAR:` / `SER:`), interpretacja tekstu oraz kodów sterujących drukarek Amigi (`ESC/P` / `ISO`) i przekierowanie wydruku do natywnego okna drukowania macOS (`NSPrintOperation` / drukarka systemowa / AirPrint / PDF).
- [x] **Opcja 512 KB Chip RAM dla A500 (rev 5 / 6a):** przełącznik między klasycznym `512 KB Chip + 512 KB Slow` a `1 MB Chip + 512 KB Slow`.

## Zgodność sprzętowa A600 / ECS (68000 + Gayle)
- [x] **Identyfikacja układów ECS (`Super Agnus 8375` i `Super Denise 8373`):** zwracanie identyfikatora `$20` (2 MB PAL Super Agnus) w `VPOSR` (`$DFF004`) oraz `$00FC` (Super Denise 8373) w `DENISEID` (`$DFF07C`), wymagane przez `graphics.library`, sterowniki monitorów ECS i programy diagnostyczne (`SysInfo`, `WhichAmiga`, `ShowConfig`).
- [x] **Autentyczna mapa pamięci A600 (`1 MB / 2 MB Chip RAM`, brak `Slow RAM` pod `$C00000`):** usunięcie nieistniejącego w A600 bloku `$C00000` oraz dodanie przełącznika rozszerzenia `A601` (`1 MB Chip RAM` vs `2 MB Chip RAM` w ciągłym obszarze `$000000–$1FFFFF`).
- [x] **Tryby wideo ECS (`Super Denise 8373`):** obsługa rozdzielczości `SuperHires` (1280×256/512, 2 bitplany z paletą 64 kolorów ECS) oraz wygaszania ramki `BRDRBLNK` (`BPLCON3` bit 5) na A600.
- [x] **Rejestry kontrolera PCMCIA Gayle (`$DA8000–$DAB000` i okno `$600000–$A3FFFF`):** poprawna sygnalizacja stanu gniazda PCMCIA (`/CC_DET`, `BVD1/2`, `WR`, `BSY`) dla `card.resource` w Kickstarcie 2.05 / 3.1.

## Zgodność sprzętowa A1200 / AGA (68EC020)
- [x] **Instruction Cache (`68EC020` 256 B I-Cache):** faktyczne buforowanie instrukcji z `CACR` (wpływ na szybkość pętli w Chip RAM-ie i obsługę samomodyfikującego się kodu).
- [x] **32-bitowy Chip RAM i arbitraż *AGA DMA contention*:** modelowanie mniejszego obciążenia szyny przez 32/64-bitowe pobrania `Lisa`/`Alice` (`FMODE`) oraz spowolnień CPU bez Fast RAM-u.
- [x] **Sprzętowy kontroler IDE Gayle (`$DA0000`):** opcjonalna obsługa `.hdf` przez wewnętrzny port IDE A600/A1200 obok obecnego kontrolera Autoconfig HD.
- [x] **Precyzja wideo AGA (`Lisa`) — część 1:** pozycjonowanie sprite'ów `SH1`/`SH0` (`SPRxCTL` bity 4:3), poprawna szerokość pikseli sprite'ów dla `SPRES` (lores/hires/SuperHires), bit `PFxH0` (35 ns) w scrollu `BPLCON1` w trybie SHRES oraz zmiany palety przez Copper w środku linii.
- [x] **Precyzja wideo AGA (`Lisa`) — część 2:** renderowanie sprite'ów z rozdzielczością 35 ns (`SH0` i `SPRES=3` z supersamplingiem 2×), pełne zmiany palety i tła `COLOR00` w połowie linii również przy zapisach przez CPU (nie tylko Copper).

## Zgodność sprzętowa A4000 / AGA (68030 + 68882)
- [x] **Natywny kontroler IDE płyty głównej A4000 (`$DD2000–$DD3FFF`):** podłączenie silnika `GayleIde` pod adresy kontrolera ATA/IDE Amigi 4000 (`$00DD2020` + status przerwań `$00DD203A`), używanego przez `scsi.device` z Kickstartu A4000.
- [x] **Data Cache (`68030` 256 B D-Cache) i tryb Burst (`CACR`):** emulacja 256-bajtowej pamięci podręcznej danych procesora 68030 (`DE`, `FD`, `CED`, `CD`, `WA`, `DBE` w `CACR` oraz bitów `Page Mode`/`Burst` w kontrolerze `Ramsey` `$DE0003`).
- [x] **Jednostka MMU `68030` (`TC`, `CRP`, `SRP`, `TT0`, `TT1`, `MMUSR`):** pełna obsługa instrukcji `PMOVE`, `PTEST`, `PFLUSH` oraz rejestrów *Transparent Translation* wymaganych przez `68030.library`, `SetPatch`, `CPU FASTROM` i narzędzia diagnostyczne (`SysInfo`, `AIBB`, `WhichAmiga`).
- [x] **Pełny zestaw instrukcji koprocesora FPU `68882`:** funkcje trygonometryczne i wykładnicze (`FSIN`, `FCOS`, `FSINCOS`, `FTAN`, `FATAN`, `FETOX`, `FLOGN`, `FLOG10`, `FTWOTOX`, `FGETEXP`, `FGETMAN`) oraz ramki stanów `FSAVE`/`FRESTORE` dla `68882` (wymagane przez programy 3D jak LightWave / Imagine / VistaPro i benchmarki FPU).
- [x] **Rejestry płyty głównej A4000 (`Super Buster` rev 11, `Gary`, `Ramsey` rev 7):** pełny odczyt/zapis rejestrów konfiguracyjnych `$DE0000–$DE1000` dla poprawnego rozpoznawania rewizji układów A4000 przez programy diagnostyczne.

## Kontrolery i urządzenia zewnętrzne
- [ ] **Portowanie joysticków z JoyPort do USB / Bluetooth:** obsługa fizycznych kontrolerów USB / Bluetooth (`GCController` / `IOHIDManager`) mapowanych na porty joysticka Amigi.