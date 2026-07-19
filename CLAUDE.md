# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Projet

Drone Looper — application audio JUCE (C++20) pour construire des nappes/drones : on enregistre un échantillon, et 4 voix de lecture indépendantes le rejouent avec des vitesses, filtres et effets différents. Cible finale : iPad.

Le code et les commentaires sont **en français** — conserver cette convention.

## Build

JUCE n'est pas vendorisé : le `CMakeLists.txt` fait `add_subdirectory("../JUCE")`. Le dépôt JUCE doit donc être un **répertoire frère** de celui-ci.

```bash
# Debug — celui à utiliser pour développer (assertions JUCE actives)
cmake --build build --target DroneLooper

# Reconfigurer après ajout d'un fichier source ou d'un module JUCE
cmake -S . -B build && cmake --build build --target DroneLooper
```

Binaire : `build/DroneLooper_artefacts/Debug/Drone Looper`.

**Ne pas construire la Release sans demande explicite** (LTO + recompilation sérielle de tout JUCE, c'est long) :

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target DroneLooper
```

Le build Debug est en `-O0` : **toute mesure de performance doit se faire en Release**, où le DSP est ~3× plus rapide. L'app affiche sa charge CPU audio sous le vu-mètre.

Il n'y a pas de tests. La vérification se fait à l'oreille — l'utilisateur lance et teste l'app lui-même.

## Architecture

Quatre fichiers dans `Source/`, avec une séparation nette entre ce qui est **partagé** et ce qui est **par voix**.

- **`MainComponent`** — détient tout ce qui est partagé : l'échantillon enregistré (`recordingBuffer`, mono), l'état enregistrement/lecture, le bouton commun, le vu-mètre, le master, le limiteur et les deux réverbes.
- **`LooperComponent`** — une voix complète et autonome (×4). Chaque instance a sa position de lecture, son filtre, sa ligne à retard et son buffer de travail.
- **`SpringReverb`** — réverbe à ressort mono, instanciée deux fois (gauche/droite) pour permettre des réglages distincts plus tard.
- **`Main.cpp`** — application et fenêtre uniquement.

### Flux du signal

```
entrée (1 ou 2 canaux) → somme mono → recordingBuffer  [échantillon partagé]
                                            │
        ┌───────────────────────────────────┴──── les 4 voix lisent le MÊME échantillon,
        │                                          chacune une PORTION tirée au sort
        ▼
  lecture Hermite → filtre → delay (+saturation du wet) → volume → pan
        ├──────────────────────────────────────────────────────────→ mix stéréo (SOMME)
        └── × send ──→ bus réverbe ──→ [Spring L | Spring R] ────────→ mix (retour à l'unité)
                                                                            │
                                                              master → limiteur → sortie
```

Points structurants, non devinables depuis un seul fichier :

- **La sortie est additive.** Chaque voix fait `addFrom()` dans un buffer effacé au préalable. Une voix ne doit jamais écraser la sortie, ni filtrer en place dedans — d'où le `renderBuffer` mono pré-alloué par instance.
- **Le send réverbe est prélevé après volume et pan**, comme sur une console : le placement stéréo d'une voix est conservé dans la réverbe.
- **La réverbe tourne en continu** pendant la lecture (pas de court-circuit) : la couper selon les sends tronquerait la queue en cours.
- **Passer à un autre nombre de voix** = changer `numberOfLoopers` dans `MainComponent.h`. Tout le reste (layout en colonnes, `prepare`, rendu) itère déjà sur l'`OwnedArray`.

### Modèle de threads

Thread message (UI) et thread audio communiquent **uniquement par `std::atomic`**, sans verrou. Discipline à respecter absolument :

- Les paramètres sont publiés par l'UI dans des atomiques, relus **une fois par bloc** par le thread audio.
- Pour une transition d'état, **modifier les données d'abord, publier le drapeau atomique en dernier** (`store` en fin de séquence). Le thread audio ne lit ces données que si le drapeau est vrai, donc l'ordre garantit la cohérence. Voir `MainComponent::toggleRecording()`.
- Réinitialiser l'état DSP d'une voix (`startPlayback`) se fait depuis le thread message **pendant que `isPlaying` est faux**.

### Contrainte temps-réel

**Aucune allocation, aucun verrou, aucun appel UI dans `getNextAudioBlock` ni dans quoi que ce soit qu'il appelle.** Tous les buffers sont dimensionnés dans `prepareToPlay` / `prepare` (échantillon 60 s, ligne à retard 2 s par voix, scratch de la taille du bloc). Les fonctions de rendu sortent proprement si le bloc dépasse la taille annoncée plutôt que de réallouer.

### Piège récurrent : les écrêteurs dans une boucle de réinjection

`softClip()` existe en deux exemplaires (`LooperComponent` et `SpringReverb`) et **doit être strictement l'identité sous son seuil**. Un `tanh` brut n'est pas l'identité aux niveaux normaux : il retire ~0,3 % par passage, ce qui sur une queue de réverbe de 16 s (des centaines de tours) représente ~14 dB de perte parasite et tue l'effet. Ce bug a déjà été introduit une fois puis corrigé.

C'est cet écrêteur qui rend le feedback à 100 % stable au lieu de divergent : il fait chuter le gain de boucle sous 1 dès que le signal dépasse le seuil.

## Constantes de réglage à l'oreille

Regroupées et commentées dans les en-têtes, à ajuster quand l'utilisateur donne un retour d'écoute :

- `LooperComponent.h` — `maxSaturationDrive`, `warmBias` (asymétrie = harmoniques paires = « chaud »), `saturationFeedbackFloor`, `loopClipThreshold`.
- `SpringReverb.h` — `allpassCoefficient` et `numberOfAllpassStages` (la cascade de passe-tout **est** ce qui fait le son de ressort : dispersion → chirp), `outputGain`, `inputHighPassHz`.

La réverbe domine le coût CPU : 3 ressorts × 32 passe-tout × 2 canaux, en chaîne sérielle donc non vectorisable.

## À prévoir pour la cible iPad

`MICROPHONE_PERMISSION_ENABLED` n'est pas encore activé dans `juce_add_gui_app` — indispensable pour la capture micro sur iOS.
