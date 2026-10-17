# Serum 2: contexto de engenharia reversa (arquitetura do plugin, DSP e wavetable)

> **Escopo e uso.** Este documento descreve *como o Serum 2 é construído*: camadas, classes,
> ordem de carga, fluxo de dados e formatos. Ele serve como referência de arquitetura para o
> **Borato Wave** (`synth/`), que é implementação **própria** (clean room). Nenhum trecho de
> pseudocódigo foi copiado para cá de propósito. Os algoritmos devem ser implementados a
> partir de fontes públicas, usando isto só como mapa do que existe e de como as peças se
> conectam. Veja `synth/docs/ARCHITECTURE.md`.

---

## 0. Proveniência e método

| Item | Valor |
|---|---|
| Binário | `C:\Program Files\Common Files\VST3\Serum2.vst3` (PE x86-64 DLL) |
| SHA-256 | `7978c9be5b2107e985c24c174000faee87e11483ec989d81dc61b5829b45ed70` |
| Versão | `2.0.21`, SDK `VST 3.7.12` (strings da factory); controller também cita `2.0.20` |
| Projeto Ghidra | `ghidra/Serum2.gpr` (Ghidra 12.1.4, análise completa salva, ~20 min) |
| Scripts | `ghidra/scripts/*.java` (ver §9) |
| Data | 2026-10-17 |

Como os fatos foram obtidos:
1. O RTTI do MSVC fornece os nomes de classe e dos vtables (`Classe::vftable`). Daí sai quase toda a nomenclatura.
2. O dono de cada função foi identificado pelo slot do vtable (classe e índice). Os índices de interfaces VST3 seguem a ordem do SDK.
3. As strings, junto com as xrefs a elas, deram a semântica: chaves JSON, nomes de mensagens, enums serializados como texto.
4. Para achar DSP, as funções foram ranqueadas por densidade de instruções SIMD float.
5. Funções-chave foram decompiladas e lidas para extrair comportamento.

Grau de confiança, usado no texto:
- **[C]** confirmado: string, RTTI ou fluxo lido no pseudocódigo.
- **[I]** inferido: deduzido da estrutura, plausível mas não provado.

> **REA × Ghidra:** o provider `ghidra` do MCP REA reanalisa o binário do zero e estoura o
> timeout. O caminho que funciona é rodar o headless direto no projeto salvo (§9), que leva ~1 min por execução.

---

## 1. Visão geral: o que existe no binário

### 1.1 Bibliotecas embarcadas [C]
| Biblioteca | Evidência | Papel |
|---|---|---|
| **VST3 SDK 3.7.12** | `Steinberg::Vst::*`, `AudioEffect`, `EditControllerEx1` | Protocolo do plugin |
| **VSTGUI 4** | `..\libraries\vst3sdk\vstgui4\...`, ~180 classes `VSTGUI::` | UI inteira (Direct2D no Windows) |
| **zstd** | recurso `serum2.uidesc` comprimido, `ZSTD_decompress`/`isError` | Descompressão do layout |
| **nlohmann::json** | erros `cannot use operator[] with a numeric argument`, enums serializados por string | Estado, presets e mensagens DSP↔UI |
| **SQLite** | mensagens de erro do SQLite | `System\presets.db` (navegador de presets) |
| **sndlib (CCRMA)** | `mus_write: ...` | Leitura e escrita de áudio (.wav etc.) |
| **libsfz** | RTTI `libsfz::Sound` | Import de multisample SFZ |
| **muParser** | RTTI `mu::Parser`, `mu::ParserBase` | Fórmulas do editor de wavetable |
| **ZL convolve** | `ZLFIRConvolve`, `ZLSingleFDLConvolve`, `ZLDualFDLConvolve` | Convolução (FX Conv, reverb) |
| **zip** | `could not finalize zip archive`, `.SerumPack.json` | Preset packs |

### 1.2 Uma DLL, dois plugins [C]
`GetPluginFactory` (init em `0x180001120`) registra **4 classes** na `S2PluginFactory`:

| Classe | Nome | Categoria | createInstance |
|---|---|---|---|
| Audio Module Class | **Serum 2 FX** | (efeito) | `0x180002a80` |
| Component Controller Class | Serum 2 FX | | `0x180002b00` |
| Audio Module Class | **Serum 2** | `Instrument\|Synth` | `0x180002b70` |
| Component Controller Class | Serum 2 | | `0x180002bf0` |

As duas variantes constroem as **mesmas** classes. `SerumDSP::SerumDSP` (`0x180031100`) é chamado por `2a80` e `2b70`. `SerumController::SerumController` (`0x1800997b0`) é chamado por `2b00` e `2bf0`. A variante FX é só uma flag. A factory também detecta o host pelo nome do executável (`\FL64.exe`, `\WaveLab`) para ajustes de compatibilidade.

---

## 2. Camada de construção do plugin (fora do protocolo puro)

### 2.1 Mapa de classes de topo
```
S2PluginFactory ─┬─► SerumDSP        (Processor: AudioEffect + IConnectionPoint + ... )
                 └─► SerumController (EditControllerEx1 + IMidiMapping + ... )
                          └─ createView() ─► SerumEditor : XferVST3Editor (VSTGUI)
                                                 └─ SerumUIDescription (serum2.uidesc)
                                                 └─ sub-controllers (≈45 classes *Controller)
```

### 2.2 `SerumDSP`: o processor [C]
É herança múltipla com 14 sub-vtables a partir de `0x1809e1df8`. Slots identificados:

| Sub-vtable | Interface | Slot → função | Observação |
|---|---|---|---|
| `1809e1e78` | IMidiMapping/IInfo [I] | [3] `0x180040f40` | usa strings de host (Bitwig, Ableton, Logic, Reason) e nomes de bus |
| `1809e1ea8` | **IConnectionPoint** | [3] `0x1800405d0` connect, [5] `0x1800541d0` **notify** (19 KB) | handshake e mensagens, ver §2.5 |
| `1809e1ee8` | **IComponent** | [11] setActive `0x180047650`, [12] **setState** `0x180041ca0`, [13] **getState** `0x180044bb0` | chaves `component`, `processor`, `demoVersion` |
| `1809e1f68` | **IAudioProcessor** | [3] setBusArrangements `0x180041300`, [7] **setupProcessing** `0x180047460`, [9] **process** `0x1800477c0` (13 KB) | buses Mono/Stereo In/Out |
| `1809e1ff8` | interface interna Serum | [4] `0x180044f20` serializa params (`plainParams`, `lockOversampling`, `mpeConfig`…), [5] `0x1800425b0` **carrega preset** (`Not a valid Serum preset.`, `serum1ChunkVersion`, `hybridize`, `tuningData`), [8] `0x1800679b0` | |

**Construtor `SerumDSP` (`0x180031100`, 3.3 K linhas)** [C/I]:
- Aloca uma **arena alinhada a 32 bytes** de `0x448B27` bytes (≈4.3 MB) e constrói objetos `Voice` dentro dela com `FUN_1803a8430`, em offsets de `0x1122C0` bytes (≈1.07 MB por `Voice`, portanto 4 blocos). [C] É um layout pensado para cache e SIMD. Que cada `Voice` agrupe várias notas em lanes SIMD é [I].
- Cria 3 `OscState` (loop `<3`, osc A/B/C) e liga cada um ao estado global. [C]
- Instancia os subsistemas: `FUN_180365b70` (0x42140 B), `FUN_180349730` (0x4748 B), `FUN_180380250` (400 B). Também reserva filas de eventos (0x10000), um pool de eventos (`MemoryPool<PoolEvent,3670072,1>`), vetores para 16 itens com capacidade `0x400` e listas de 128 notas.
- Registra blocos de parâmetros por módulo. Os laços com contagens 5, 2, 4, 10, **64** (mod slots), 8 (macros), 7, 12 e 3×16 (`VoicePanel`) referenciam `PTR_s_VoiceFilter`, `PTR_s_VoicePanel` etc. Os nomes de enum de parâmetros ficam embutidos como strings (`kParamEnable = 0, kParamFreq, …`) e são serializados por nome.

**`setupProcessing` (`0x180047460`)** [C]:
- Rejeita `maxSamplesPerBlock > 16384` (`0x4000`) quando uma flag está ativa.
- Protege a troca de configuração com um **spinlock de 1 byte** (`+0x116986`).
- Se o sample rate mudou, chama `FUN_18003b960` (reset ou recálculo).
- Se `processMode == kOffline (2)` e a pref `kPref_b_UseUltraOnRender` estiver ligada, força **oversampling = 4**. Fora disso usa `1 << kPref_i_DefaultOversamplingLevel`. Essas duas regras só valem se `lockOversampling` não estiver setado.
- Propaga a flag *offline* para todos os módulos FX por uma chamada virtual (+0x68).

**`process` (`0x1800477c0`)** [C/I]:
1. Lê o transporte do host: tempo, PPQ, compasso e play state, via `FUN_1803eb*`/`1803ec*`.
2. Consome eventos (notas, MPE/NoteExpression, CC, pitch bend) em ordem de sample-offset, partindo o bloco nos pontos de evento (laço `while (offset < numSamples)`).
3. Processa clip player e arp (`ClipPlayer`, `Arp`), que geram notas internas.
4. Faz a varredura de 128 notas e 128 CCs (laço até `0x80`).
5. Chama **`FUN_180052d40` = render principal** (ranqueada em SIMD). Ela despacha para `18008fec0`, `180084f70` (14 KB, SIMD alto) e `18008c080`: soma de vozes, FX rack e saída.
6. Fecha o bloco: entrega trabalhos pendentes dos osciladores, como aplicar wavetables carregadas (`FUN_1802fab00`) e estados de osc (`FUN_1802ead90`), além de metering.

### 2.3 `SerumController`: o edit controller [C]
Vtable principal em `0x1809e9528`, com ~97 slots próprios. Os mais relevantes:

| Slot | Função | Papel (pelas strings) |
|---|---|---|
| [33] `0x1800ac750` | carregar preset (`Are You Sure?`, `defaultPreset`) |
| [34] `0x1800c2940` | hybridize / carga parcial de módulos (`ModSlot`, `Global`) |
| [35] `0x1800c8410` | shapes (`XferShape`) |
| [36–38] | FX preset (`SerumFX`) e FX rack (`SerumFXRack`) |
| [39] `0x1800bf3a0` | metadados de preset (`presetName/Author/Description/AuthorLocked`) |
| [40–43] | clips e arp (`XferClip`, `XferClipBank`, `XferArp`, `XferArpBank`) |
| [44] `0x1800d4960` | salvar preset |
| [49] `0x1800cc7e0` | import de arquivos legados (Serum 1 preset, MIDI map, `.tun`) |
| [52] `0x1800ceb80` | **salvar wavetable** (`Save WaveTable as...`, `.256 File`, `Wav File`) |
| [53] `0x1800cfa50` | salvar sample |
| sub-vtable `1809e98b8` [5–7] | IEditController setComponentState / setState / getState |
| sub-vtable `1809e98b8` [17] `0x1800b6500` | **createView** → `new SerumEditor` (`0x1804660c0`) |
| sub-vtable `1809e9878` [5] `0x1800a51b0` | **notify** (mensagens vindas do DSP, §2.5) |
| sub-vtable `1809e9848` [3] `0x18009d420` | init e preferências: todas as `kPref_*`, detecção de host (FL, Reaper, Cubase, Bitwig, Renoise…), nomes de grupos de parâmetros (`Mod Matrix`, `FX (Bus 1)`…) |

Preferências globais (`kPref_*`) ficam num singleton acessado por `FUN_180021140()`. Exemplos: `DefaultOversamplingLevel`, `DefaultZoomSize`, `NumOutputPairs`, `MW_To_WTEditor`, `Do_NotNormalizeSingleCycles`, `DisableDirectComposition`, `UseUltraOnRender`, `SmoothRatePercent`.

> Observação: o init do controller (`0x18009d420`) referencia as strings
> `"Hello TEAM R2R's witch"` e `"RIP Gouda --=OxYGeN/H2O/AiR=--"`. Elas citam grupos de
> cracking. Ou é uma checagem anti-pirataria do fabricante, ou o binário foi modificado.
> Isso **não foi investigado** e está fora do escopo.

### 2.4 Parâmetros [C]
- `SerumParameter` é a base. Há subclasses com curva própria: `LogParameter`, `ScaledParameter`, `TimeParameter`, `PercentParameter`, `PanParameter`, `GainParameter`, `ListParameter`, `WarpParameter`, `WTPosParam`, `FilterFreqParameter`, `EnvTimeParameter`, `LFORateParameter`, `DelayTimeParameter`, `CompRatioParameter`, `OscPitchParameter`, `NoiseColorParameter`, `ProxyParameter`, entre outras.
- Os parâmetros são criados em `SerumDSP::createParams(int, ParameterContainer*, ParameterContainer*)` (nome visível no RTTI de um lambda local `GlobalTuningParam`). O mesmo código serve DSP e controller.
- Cada módulo declara um enum `kParam… , kNumAutomatableParams, kNumParams, kNumPrivateParams = kNumParams - kNumAutomatableParams`. Ou seja, há parâmetros **automatizáveis** e **privados** (só estado, não expostos ao host).
- O estado é JSON: `plainParams` guarda valores em escala real, não normalizados, e `linkedParams`/`lockedParams` também entram. O preset é empacotado por `XferJson::compress`.

### 2.5 Protocolo DSP ↔ UI (IConnectionPoint + JSON) [C]
**Handshake** (`SerumDSP` connect `0x1800405d0`): o DSP envia uma `IMessage` `"Connected"` com dois atributos:
- `"IConnectionPoint"` = ponteiro para si mesmo;
- `"SerumDSPSharedData"` = **ponteiro bruto** para `this+0x179c20`.

O controller passa a ler e escrever esse bloco compartilhado direto na memória. Isso assume DSP e UI no mesmo processo e quebra num host distribuído. Para o Borato Wave, preferir FIFOs lock-free.

**UI → DSP**: mensagens tratadas em `SerumDSP::notify` (`0x1800541d0`):
`PollForMessages`, `AddToJson`, `LoadJson`, `InitParams`, `InitModule`, `SwapModules`, `CopyOsc`, `ConvertOscType`, `LoadTable`, `SelectTableFromPath`, `ForceTableLength`, **`PerformWTEditAction`**, `ProcessWTBuffer`, `ProcessWTMakeUndo`, `ProcessWTReorderTables`, `SetWTStartPhases`, `WTEditorSelection`, `SetEmbedContent`, `GetEmbedInfo`, `LoadSpectralFilterTable`, `LoadIR`, `LoadLoop`, `LoadSliceMarkers`, `SetSliceMarkers`, `ReplaceMultisample`, `SampleEdit`, `Normalize`, `FadeEdges`, `CurveEdit`, `LFOEdit`, `LFOPathData`, `LFOToNoiseOsc`, `ModMatrix`, `ModSlot`, `DuplicateMods`, `MIDILearn`, `ClearMIDIMap`, `MPEConfig`, `MPEEnabled`, `MPEBendRange`, `LockTuning`, `ClearTuning`, `NoteFromGUI`, `ClipEditAction`, `ClipEvent`, `ClipLoadData`, `ClipRecordEvent`, `RetriggerActiveArpClip`, `FXRackMessage`, `OfflineRender`, `StereoRender`, `InputCapture`, `MeteringFlags`, `PrivateParamChange`, `SaveAll`, `ConsolidateContent`…

**DSP → UI**: `SerumController::notify` (`0x1800a51b0`):
`Connected`, `PresetEdited`, `ModMatrixChanged`, `ModDestsChanged`, `UpdateGraph`, `LFOEdit`, `LFOType`, `MIDILearn`, `MPE`, `MPEEnabled`, `NoteToGUI`, `OscLoadError`, `WTOsc`, `WTUndoAdded`, `DumpSlicesToClip`, `PrivateParamChange`, `ProcessSetup`, `Transport`, `ResetBankSelectCCVal`, `Dialog`.

Os payloads são objetos JSON com chaves como `OscID`, `Action`, `Value`, `SrcTable`, `DestTable`, `FilePath`, `LoadType`, `ParamID`, `JsonPtr`. O padrão é: **a UI nunca altera o estado de áudio diretamente; ela envia um comando nomeado e o DSP executa**. O próximo `PollForMessages` drena as respostas.

---

## 3. Carga do editor (PluginEditor)

### 3.1 Sequência [C]
```
host → IEditController::createView("editor")
  └ SerumController slot17 (0x1800b6500)
      └ new SerumEditor(controller, …)            0x1804660c0
          ├ uidesc = loadSerumUIDescription()     0x1805e7da0
          │    FindResourceA(hModule, "serum2.uidesc", "DATA")
          │    LoadResource/LockResource/SizeofResource
          │    ZSTD_decompress → buffer estático de 3 MB (0x300000), feito 1×/processo
          │    CMemoryStream → new SerumUIDescription (subclasse de VSTGUI::UIDescription)
          ├ XferVST3Editor::XferVST3Editor(uidesc, controller, "Empty", "serum2.uidesc")  0x18053d4e0
          │    (VST3Editor customizado: focus drawing, "Show Editor Button", context menus)
          ├ herda também PrefsListener, IBitmapCreator2, IKeyboardHook, ITextEditListener,
          │  ViewListenerAdapter
          ├ registra-se na lista global de editores abertos (singleton de prefs, +0x1c0)
          └ busca o parâmetro de zoom/tamanho (id 0x1b5bc8c1) no controller
SerumController::editorIsBeingOpened(SerumEditor*, bool)  → diálogos de primeira execução
  ("Locate Serum 2 Presets folder from install")
```

- O editor abre no template **`"Empty"`** e depois troca para os templates reais. Os nomes registrados por `*TemplateParams` são `MainUI` (`0x18038bff0`), `SynthUI` (`0x18038ce50`), `Splash` (`0x18038cdb0`), além de `WTEditorFFTs`, `CurveEditor`, `ZoneEditor` e `PZEditor`. [C]
- Cada `XxxTemplateParams` (≈60 classes) define parâmetros de UI (`kUIParam…`) daquele template, como `kUIParamWaveformEdit, kUIParamWaveformFFTEdit, kUIParamWaveformFFTPhasesEdit, kUIParamFFTZoomWid…`. São parâmetros só de interface, fora do host.
- Recursos gráficos: bitmaps do uidesc (`bitmaps`), cores nomeadas (`frameHighlightColor`, `Wavetable3DInterpColor`, `FFTGradientTop/Middle/Bottom`…). Usa filtros de bitmap VSTGUI (`BoxBlur`, `ScaleLinear`, `ReplaceColor`) para glow e sombra.

### 3.2 Sub-controllers (o "MVC" do Serum) [C]
`SerumEditor::createSubController` (`0x18046d870`, mais o slot [16] `0x180467430` para os de diálogo) instancia por **nome declarado no uidesc**:

`ModMatrixController, LogoMenuController, MainMenuController, FXListController, DragExportController, ArpClipSlotController, RoutingSlotController, PresetController, ClipPlayerKeysController, ArpKeysController, MacrosController, MissingFilesController, ZoneEditorController, PZEditorController, WTEditorControllller`*, `LFOEditorController, PitchQuantizerController, ClipPlayerController, FilterController, PresetBrowserController, ArpClipSlotContainerController, MidiClipSlotContainerController, ArpController, UndoController, ClipPlayerPianoRollController, ArpPianoRollController, VoicePanelController, GlobalController, FXRackController, SplashController, PresetPackAuthoringController, ScalarCurveController, EnvelopeController, ClipPlayerAutomationLaneController, ModSourceController, SpliceController, ClipPlayerNoteGridController, ArpNoteGridController, ClipPlayerTabController, ArpTabController, LFOController, CurveEditorController, DialogController`.

\* A string no binário tem mesmo o erro de digitação `WTEditorControllller`. O uidesc usa esse nome.

- O **`OscTypeController`** (`0x18076d230`) troca o sub-controller conforme o tipo do oscilador: `WTOscController`, `SubOscController`, `NoiseOscController`, `MultiSampleOscController`, `GranularOscController`, `SpectralOscController`. Os de sample usam o template `SampleBasedOscController<State, Display>` e `XYSampleBasedOscController<…, YAxisAssignment>`. [C]
- A base é `SerumSubController : XferSubController` (com `ControlMap`). Ela traz menu de contexto, MIDI learn, mapeamento de FX para proxy (`mapFXParameterToProxy`) e undo (`MIDILearnUndoAction`). [C]
- Views customizadas entram no uidesc por **`XferViewCreator<T>`**, com ≈95 tipos: `WTDisplay2D/3D`, `WTPhaseEdit`, `WTImportPanel`, `WavetableDropTarget`, `FFTDisplay`, `SpectrogramDisplay`, `GranularDisplay`, `LFODisplay`, `EnvelopeDisplay`, `FilterDisplay`, `PianoKeyboard`, `XferKnob`, `VectorKnob`, `XferAnimKnob` (filmstrip), `XferParamDisplay`, `XferVuMeter`… [C]

---

## 4. Camada DSP (PluginDSP)

### 4.1 Estado × processamento [C]
O Serum separa **estado serializável** (`*State`) de **processador de voz** (sem sufixo):

| Estado (preset/UI) | Processador (por voz) |
|---|---|
| `OscState` (contém **todos** os sub-estados de tipo) → `WTOscState`, `SampleOscState`, `MultiSampleOscState`, `GranularOscState`, `SpectralOscState`, `NoiseOscState`, `SubOscState` | `WTOsc`, `SampleOsc`, `MultiSampleOsc`, `GranularOsc`, `SpectralOsc`, `NoiseOsc`, `SubOsc` |
| `EnvState`, `LFOState`, `RetriggerState` | `LFO` (+`LFO::ModSource`), envelopes na `Voice` |
| `ModSlot`, `ModMatrix`, `Macro` | `RoutingSlotLive` (por voz), `LFOPointModBusLive` |

- `OscState` (ctor `0x18029c530`) constrói **todos** os estados de tipo de uma vez. Trocar o tipo do osc (`ConvertOscType`) não realoca nada. [C]
- Slots do vtable de `OscState`/`*OscState`: [2] reset/default, [4] **toJson**, [5] **fromJson** (`kParamType`, `kOsc_WT`). [C]
- Enum de tipos [C]: `kOsc_WT=0, kOsc_MultiSample, kOsc_Sample, kOsc_Granular, kOsc_Spectral, kOsc_Noise, kOsc_Sub`.

### 4.2 Voz [C/I]
`Voice` (ctor `0x1803a8430`) contém:
- 8 `RoutingSlotLive` (roteamento por voz);
- 2 instâncias de filtro de voz (`"FXFilter"`/`VoiceFilter`, ~0x10670 B cada);
- `LFO` com 2 `LFO::ModSource`;
- buffers de vários KB.

Os processadores de osc da voz são criados pela fábrica `0x1803aa7b0`, que chama os ctors por tipo: `WTOsc` em `0x1803b4fb0`, os demais em `1803bc250`, `1803c0e90`, `1803c5f70` e `1803ce120`. `NoteExpressionVoice` cobre MPE.

### 4.3 Filtros e FX (process por slot de vtable) [C]
- **Filtros** herdam de `XferFilterBase`, com `process(block)` no **slot [3]**: SVF `0x18026a270` (maior função DSP do binário), CombFlangePhase `0x1802705f0`, Wild `0x180274d80`, DirtyMoog `0x180268120`, Moog `0x180269ac0`, EQ `0x180273f00`, Misc `0x180277e70`, Formant `0x180279ce0`, German `0x18027ab10`, PZ `0x18027b430`, Comb2 `0x180273840`, além de Ladder, SEM, SNH, Wasp, DJM e Diffuser.
  - O SVF tem um laço por amostra para cada modo (`switch` fora do laço). É TPT/ZDF, e as variantes com drive aplicam soft-clip cúbico `s -= k·s³` no integrador.
- **FX** herdam de `XferModule`, com `process` no **slot [16]**: `XferReverb` (Plate/Hall/Vintage/Abyss/Space, com `xfer_plate_resampling`), `XferChorus` `0x1802be1f0`, `XferFlanger` `0x1802bc2e0`, `XferPhaser` `0x1802bd220`, `XferDelay` `0x1802bfdc0`, `XferDist`, `XferHyper`/`XferHyperD`, `XferDimE`, `XferEQ`, `XferComp`, `XferConv`, `XferBode`, `XferUtils`, `XferFilter`, mais os splitters `FXSplit`/`FXSplit3`/`FXSplitMS`.
- Funções matemáticas como sin e exp são **polinômios inline**, quase sem libm, o que dá a densidade SIMD alta.

---

## 5. Wavetable: modelo, criação, import, edição, render

### 5.1 Modelo de dados [C]
- **Frame = 2048 amostras float32** (stride `0x2000` bytes). Os arquivos gerados levam o marcador `"<!>2048 00000000 wavetable (www.xferrecords.com)"`, a convenção do chunk `clm ` em WAV do Serum: tamanho do frame e flags.
- Uma wavetable tem **até 256 frames** (formato `.256 File` no "Save WaveTable as…"). `NumTables`, `numFrames`, `numChannels` e `sampleRate` são mantidos por osc.
- Tabela padrão: `S2 Tables\Default Shapes.wav`. Noise padrão: `Organics\AC hum1.wav`.
- Persistência (`WTOscState::toJson`, `0x1802fab30`), chaves JSON:
  - `pathToWT` / `relativePathToWT`: caminho relativo à pasta de tabelas, com `\`;
  - `loadType`: enum abaixo;
  - `numChannels`, `numFrames`, `sampleRate`: gravados só se ≠ 0;
  - `storedPhasePos`;
  - **`embeddedWTData`**: só quando a tabela foi editada (flag `+0x42e61`). Grava **apenas os frames marcados como sujos** num bitset, em blocos de 2048 floats;
  - `interpolateAfterLoad`, `tableDisplayName`.
  - Se o caminho é o default (`" Default"`), nada é salvo.
- `fromJson` (slot [5]) e `FUN_1802a4cb0`, este último chamado por `LoadTable`/`SelectTableFromPath`/preset, respondem à UI com `OscID`, `NumTables`, `GuessedFrameSize` e `OscLoadError`.

### 5.2 Tipos de carga / análise de áudio (`loadType`) [C]
Enum serializado (`0x180318c30`, padrão `NLOHMANN_JSON_SERIALIZE_ENUM`):

| Valor | Nome | Menu na UI |
|---|---|---|
| 0 | `DynamicPitchZeroSnap` | Import Audio (Dynamic Pitch Zero-Snap) |
| 1 | `DynamicPitchFollow` | Import Audio (Dynamic Pitch Follow) |
| 2 | `PitchAvg` | Import Audio (Pitch Avg) |
| 3–6 | `FFT256`, `FFT512`, `FFT1024`, `FFT2048` | Import Audio FFT Resynthesis (tamanho do frame) |
| 7 | `SingleTable` | Import Single-Cycle Waveform |
| 8 | `ForceLength` | "Enter Number of Samples per Frame" / `ForceTableLength` |
| 9 | `FrequencyEstimation` | Import Frequency Estimation |

Em resumo: ao importar áudio arbitrário, o Serum **fatia em ciclos** por detecção de pitch (dinâmica, média ou estimativa de frequência) ou **ressintetiza via FFT** com janelas de 256 a 2048. Depois reamostra cada ciclo para 2048 amostras. A pref `Do_NotNormalizeSingleCycles` controla a normalização.

### 5.3 Fontes de criação [C]
| Origem | Onde | Detalhe |
|---|---|---|
| Arrastar arquivo(s) | `WavetableDropTarget` [5] `0x180849ef0` | **várias soundfiles**: cada arquivo vira um single cycle (frame); **imagem PNG**: a luminância dos pixels vira amplitude; split em N amostras com tom estimado (`split at: %i samples (%i Hz, note: %s%i and %i cents)`) |
| Painel de import | `WTImportPanel` | modos do enum §5.2 |
| Fórmulas | muParser; arquivos `FormulaFactorySingles.txt`, `FormulaFactoryMultis.txt`, `FormulaUserSingles.txt`, `FormulaUserMultis.txt` (`0x1806510a0`) | fórmula *single* gera 1 frame, *multi* gera vários (variável de frame/tabela); "Enter Formula Name" salva fórmula do usuário |
| Edição FFT | `0x180651a50` ("FFT Mags" / "FFT Phases") | Clear All, Clear HF/LF (bin N), Randomize Low 16/32/64 Bins (± with Half), Draw Odd/Even Harmonics Only, Generate Saw, Shift Octave Up/Down, Scale Freq Values by Bin Index, Repeat Bin Group, Progressive Fade, Create Random Series Gaps, Snap Vertical Draw to Quarters |
| Morph entre frames | `0x18064cc00` | "Morph - Crossfade", "Remove Morph Tables"; exige ≥ 2 tabelas ("Can Not Morph: Only One Table") |
| Redução | `0x1806477e0` | "Reduce to", "128 (Keep 1/2)" (decimar frames) |
| Gravação | `WTEditorController` [13] | "Recording to frame: %2i" (resample do próprio som / input capture) |
| Render de warp | menu principal | "Render OSC Warp" / "unable to render Warp": "assa" o warp atual nos frames |
| Export | controller [52] | `.256` / `.wav`; "Export All as Single-Cycle Waves" (`_%03i.wav`); temporários via sndlib a **44100 Hz, mono, float32** (`0x18040a270`, chamado por `0x1802fd480`) |

### 5.4 Ciclo de edição (UI → DSP) [C]
```
WTEditorController (UI)
  └ ação do usuário → FUN_180429f80: msg "PerformWTEditAction" {OscID, Action, Value, WTEditorSelection}
       → IConnectionPoint → SerumDSP::notify
           ├ ProcessWTMakeUndo  (snapshot para undo; a UI recebe "WTUndoAdded")
           ├ ProcessWTBuffer    (aplica a edição nos frames)
           └ ProcessWTReorderTables {SrcTable, DestTable} (FUN_18042a1c0)
       → DSP marca frames sujos (vão para embeddedWTData) e avisa a UI ("WTOsc"/"UpdateGraph")
```
A troca da tabela usada pelo áudio acontece **no fim do `process()`** (`FUN_1802fab00` por osc), não no meio do bloco. Isso é troca segura no thread de áudio. [C/I]

### 5.5 Render do oscilador WT [C/I]
- `WTOsc` vtable `0x180a2a4b8`: [7] `0x1803b5550` = **render por bloco**, que chama o kernel `0x1803b8a90`; [9] `0x1803afe00` = **sorteia 16 fases aleatórias** (uma por voz de unison, máximo 16) no note-on ou no reset. A mensagem `SetWTStartPhases` e os params `kParamInitialPhase`/`kParamRandomPhase` controlam isso.
- **Kernel de interpolação** (`0x1803b8a90`):
  - A fase vira índice de ponto fixo: os bits altos (`>>9`) dão o índice da amostra e os **9 bits baixos** escolhem uma de **512 sub-fases**.
  - Cada sub-fase tem **16 taps** (64 bytes), e os coeficientes são interpolados linearmente entre duas tabelas globais de coeficientes (`0x181778b00` e `0x181780b00`).
  - Isso é um **interpolador polifásico de 16 pontos (sinc janelado)**, não linear nem Hermite.
  - Uma segunda leitura opcional, ponderada, mistura outra tabela: morph entre frames ou mip-levels. [I]
- **Warp no domínio da fase**, antes da leitura: flags em bitmask (`0x4`, `0x400`, `0x40`…) para Warp 1 e Warp 2. Cada um tem modo e quantidade, e o modo **9 = `kFlip`** inverte a polaridade (bate com o enum).
  Enum de warp do WTOsc [C]: `kNoWarp=0, kSync, kBendPos, kBendNeg, kBendPosNeg, kPWM, kASYMPos, kASYMNeg, kASYMPosNeg, kFlip, kDLM, kRemap_1..4, kQuantize, kEvenOdd, kFilterLPF, kFilterHPF, kDist* (Tube, SoftClip, HardClip, Diode1/2, LinFold, SinFold, ZeroSquare, Asym, Rectify, SineShaper, StompBox, TapeSat, SoftSat), kFM_/kFMX_/kFMP_/kPD_/kAM_/kRM_ × {OSC, OSC2, NOISE, SUB, FILT1, FILT2}, kSelfPD, …`.
  Os modos FM, PD, AM e RM recebem **outra fonte** como moduladora: outro osc, noise, sub ou a saída de um filtro.
- Parâmetros do WTOsc [C]: `kParamWarp, kParamWarpVar, kParamWarpMenu, kParamWarp2, kParamWarpVar2, kParamWarpMenu2, kParamTablePos, kParamUnisonWTPos, kParamInitialPhase, kParamRandomPhase` (mais os comuns de osc: volume, pan, octave, pitch, fine, unison…).
- Unison: `kParamDetuneMode, kParamUnisonRange, kParamUnisonRotate, kParamUnisonSpanBipolar`, além de `UnisonOverview` na UI e `OscDetuneRandomizeParameter`.
- Mipmaps: os band-limits por oitava não foram localizados com certeza. A tabela de coeficientes e o "FFTOctave" sugerem geração por FFT. **[aberto]**

### 5.6 Outros osciladores (para contexto) [C]
- **Spectral**: render em `SpectralOsc[7]` `0x1803cea10` → `0x1803d5820`. Usa FFT de até 2048 bins complexos; `0x1803d7f90` faz operações espectrais (escala, interpolação entre espectros, ajuste de módulo). Também é chamada pelo `SpectrogramDisplay` e pelo editor de curva.
  Warps espectrais: `kDetune, kSmear, kSpread, kAddharmonics, kAddsubharmonics, kGate, kRobotize, kSpectralShift, kMirror, kPeakFollow, kPeakOctaveUp/Down, kPeakHarmUp/Down/Sweep, kShepardNarrow/Filter, kSpectralComb, kSpectralPitchShift(New), kSpectralPhaseTwist, kSpectralFormantShift, kMask_*, kVocode_*, …`.
  Possui tabela de filtro própria (`filterTableNumFrames`, `filterTableSampleRate`, `LoadSpectralFilterTable`).
- **Granular**: `GranularOsc[7]` `0x1803c66d0`. A função `0x1803cdb90` calcula a **janela do grão**, com 10 formas que batem com o enum `kWindowHann, Welch, Gaussian, BlackmanHarris, Sinc, Tukey, Triangle, Trapezoid, ExpDec, ExpDecRev`. Os parâmetros são forma, quantidade e **skew** (posição do pico, que remapeia a fase em dois trechos lineares). É usada também pela UI (`GranularWindowControl`, `GranularDisplay`).
  > **Correção** da análise anterior da conversa: essa função **não** é warp do WT nem shape de LFO. É a janela granular, o que o mapa de callers e o enum confirmam.
- **Sub**: `kSine, kRoundRect, kTriangle, kSaw, kSquare, kPulse`. **Noise**: `Sample, White, Pink, Brown, Geiger` (cor). **Sample/MultiSample**: slicing, `sliceMarkers`, SFZ via libsfz.

---

## 6. Índice de endereços

| Endereço | O quê | Conf. |
|---|---|---|
| `0x180001120` | init da `S2PluginFactory` (registra 4 classes) | C |
| `0x180031100` | `SerumDSP::SerumDSP` | C |
| `0x1800997b0` | `SerumController::SerumController` | C |
| `0x180047460` | `SerumDSP::setupProcessing` | C |
| `0x1800477c0` | `SerumDSP::process` | C |
| `0x180052d40` | render principal (chamado por process) | C |
| `0x180041ca0` / `0x180044bb0` | `SerumDSP::setState` / `getState` | C |
| `0x1800425b0` | carga de preset (JSON) | C |
| `0x1800405d0` | connect + handshake `SerumDSPSharedData` | C |
| `0x1800541d0` | `SerumDSP::notify` (comandos da UI) | C |
| `0x1800a51b0` | `SerumController::notify` (eventos do DSP) | C |
| `0x18009d420` | init do controller, prefs, detecção de host | C |
| `0x1800b6500` | `createView` | C |
| `0x1804660c0` | `SerumEditor::SerumEditor` | C |
| `0x18053d4e0` | `XferVST3Editor::XferVST3Editor` | C |
| `0x1805e7da0` | loader do `serum2.uidesc` (recurso + zstd) | C |
| `0x18046d870` | `createSubController` (fábrica principal) | C |
| `0x18076d230` | `OscTypeController`: sub-controller por tipo de osc | C |
| `0x18029c530` | `OscState::OscState` | C |
| `0x1802f8fb0` | `WTOscState::WTOscState` | C |
| `0x1802fab30` | `WTOscState::toJson` | C |
| `0x1802fbba0` | `WTOscState` default (Default Shapes.wav) | C |
| `0x1802a4cb0` | carregar tabela no osc / responder UI | C |
| `0x180318c30` | enum `loadType` ↔ string | C |
| `0x1802fd480` / `0x1802f1f00` | gravar WT/tabela-filtro temporária (preset pack) | C |
| `0x18040a270` | `mus_write` (sndlib) | C |
| `0x180599800` | criação de preset pack (zip, `.SerumPack.json`) | C |
| `0x18064cc00` | menu import/morph do WT editor | C |
| `0x180651a50` | operações de FFT (mags/fases) | C |
| `0x1806510a0` | listas de fórmulas | C |
| `0x180849ef0` | `WavetableDropTarget` (drop de áudio/PNG) | C |
| `0x180429f80` | envia `PerformWTEditAction` | C |
| `0x18042a1c0` | envia `ProcessWTReorderTables` | C |
| `0x1803a8430` | `Voice::Voice` | C |
| `0x1803aa7b0` | fábrica de osc por voz | C |
| `0x1803b4fb0` | `WTOsc::WTOsc` | C |
| `0x1803b5550` | `WTOsc` render por bloco | C |
| `0x1803b8a90` | kernel WT (polifásico 16 taps × 512) | C/I |
| `0x1803afe00` | `WTOsc`: 16 fases aleatórias de unison | C |
| `0x1803cdb90` | janela granular (10 formas) | C |
| `0x1803d5820` | render espectral | C |
| `0x1803d7f90` | operações espectrais (bins) | C |
| `0x18026a270` | `XferFilterSVF::process` | C |

---

## 7. Lições de arquitetura para o Borato Wave (equivalentes JUCE, sem copiar implementação)

| Ideia observada no Serum | Como fazer no Borato Wave |
|---|---|
| Um binário com variante instrumento e variante FX | dois targets CMake com o mesmo código e `IS_SYNTH` diferente, ou ignorar por enquanto |
| Estado (`*State`) separado do processador por voz | `WavetableState` (mensagem/JSON) vs `WavetableVoiceOsc` (só leitura no audio thread) |
| Todos os tipos de osc alocados de antemão | `std::variant` ou membros fixos, sem `new` ao trocar de tipo |
| Comandos nomeados UI→DSP + poll | `juce::AbstractFifo` com structs de comando; nada de ponteiro compartilhado cru |
| Troca de wavetable só no fim do bloco | double buffer + `std::atomic<Table*>` trocado entre blocos; liberar no message thread |
| Frames de 2048 float, até 256 frames, `clm ` chunk | mesmo layout dá interoperabilidade com `.wav` do Serum (o formato é público e documentado pela comunidade) |
| Import por pitch-tracking ou FFT | implementar a partir da literatura (YIN/autocorrelação; STFT + resynthesis) |
| Interpolador polifásico 16×512 | sinc janelado próprio (Kaiser), coeficientes gerados no init |
| Persistir só frames editados | marcar frames sujos e embutir apenas eles no estado do plugin |
| UI declarativa (uidesc) + sub-controllers | um `Component` por seção (já no ARCHITECTURE.md) |

---

## 8. Questões em aberto
1. Geração dos mipmaps (bandlimit por oitava): onde e com que FFT. Ponto de partida: callees de `WTOsc[7]` além do kernel.
2. Se `Voice` (4 × 1.07 MB) agrupa várias notas em lanes SIMD, e qual é o limite de polifonia.
3. Parsing do chunk `clm ` no import (rotina de leitura do sndlib e quem consome `<!>2048`).
4. Semântica exata das flags do kernel WT (`0x4`, `0x40`, `0x400`, `0x4444`).
5. Conteúdo do `serum2.uidesc`, que exige descompressão zstd. Ler **apenas para nomes e estrutura**; não reutilizar layout nem arte.

## 9. Como reproduzir
Scripts em `ghidra/scripts/`. Execução (Git Bash):
```bash
"$GHIDRA_INSTALL_DIR/support/analyzeHeadless.bat" "C:/Users/filipe/projects/rea/ghidra" Serum2 \
  -process Serum2.vst3 -noanalysis -readOnly -scriptPath "C:/Users/filipe/projects/rea/ghidra/scripts" \
  -postScript Decomp.java  <lista_de_enderecos.txt> <pasta_saida>  < /dev/null
```
- **Não** passe regex com `|` como argumento: o `.bat` interpreta o pipe. Use arquivo (`Deep.java`/`StrX.java` leem o padrão de arquivo).
- Sempre redirecione `< /dev/null`. Em caso de erro, o `.bat` fica esperando "Pressione qualquer tecla".

| Script | Faz |
|---|---|
| `Rtti.java` | lista vtables e descritores RTTI |
| `DspRank.java` | ranqueia funções por densidade SIMD e decompila as top N |
| `VtMap.java` | slots dos vtables DSP e dono das funções do ranking |
| `Deep.java` | vtables de classes (regex em arquivo) com ctors e strings, e busca de strings com xrefs |
| `Decomp.java` | decompila uma lista de endereços para arquivos `.c` |
| `StrX.java` | strings (regex em arquivo) e funções que as usam |
| `FStr.java` | todas as strings referenciadas por cada função |
| `Bfs.java` / `Up.java` | grafo de chamadas para baixo e para cima, com anotação de dono de vtable |
