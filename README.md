# Borato Waves

Sintetizador wavetable desenvolvido em C++20 com JUCE, disponivel como plugin **VST3** e aplicativo **Standalone**.

## Recursos

- Osciladores wavetable, suboscilador e ruido, com unison, FM e modelagem de onda.
- Reproducao de samples, editor de wavetables e importacao de audio.
- Envelopes, LFOs, matriz de modulacao e macros.
- Filtros e efeitos, incluindo chorus, equalizador e compressor.
- Banco de presets de fabrica e salvamento de presets do usuario.
- Entrada MIDI e suporte ASIO no Standalone para Windows.

## Compilar

Requisitos: CMake 3.22 ou superior, compilador C++20 e ferramentas de desenvolvimento da plataforma. No Windows, use um ambiente de desenvolvimento do Visual Studio.

O comando abaixo baixa a revisao fixada do JUCE 9.0.4:

```sh
cmake -S . -B build -DBORATOWAVE_FETCH_JUCE=ON -DBORATOWAVE_BUILD_TESTS=OFF
cmake --build build --config Release --target BoratoWave_VST3 BoratoWave_Standalone --parallel 2
```

Os binarios ficam em `build/BoratoWave_artefacts/Release/`. Para Linux, dependencias, testes Debug e uso de um checkout local do JUCE, consulte [BUILDING.md](BUILDING.md).

## Instalar e usar

Copie o bundle completo `Borato Waves.vst3` para a pasta de plugins da plataforma:

| Plataforma | Pasta VST3 |
|---|---|
| Windows | `C:\Program Files\Common Files\VST3\` |
| macOS | `~/Library/Audio/Plug-Ins/VST3/` |
| Linux | `~/.vst3/` |

Atualize a lista de plugins na DAW e carregue Borato Waves em uma pista de instrumento MIDI. No Standalone, configure os dispositivos de audio e MIDI antes de tocar.

## Compilacao automatica

- **Pull requests para main/master:** compilacao VST3 no Ubuntu.
- **Tags de versao `v*`:** VST3 e Standalone para Windows x64, Linux x64, macOS Apple Silicon e Intel, mais testes Debug no Ubuntu.
- Os pacotes e checksums sao reunidos em uma release em rascunho somente depois das verificacoes.

A tag deve corresponder a versao declarada no CMake. Veja o procedimento em [BUILDING.md](BUILDING.md).

## Estado do projeto

Projeto em desenvolvimento. Os pacotes Windows nao possuem assinatura comercial; os pacotes macOS usam assinatura ad hoc, sem notarizacao. A validacao em DAWs e dispositivos de audio reais complementa os testes automatizados.

Os motores completos de multisamples, sintese granular e ressintese espectral ainda nao fazem parte desta entrega.
