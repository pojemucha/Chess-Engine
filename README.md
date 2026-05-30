# MyChess Engine ♟️

**MyChess** is a high-performance, UCI-compliant chess engine written in modern C++20. Built from scratch, it features an advanced bitboard-based move generator, a highly optimized Alpha-Beta search architecture, and a standalone Graphical User Interface (GUI) powered by SFML.

Additionally, the engine features a built-in **Machine Learning Tuner** utilizing Texel's Tuning Method (Stochastic Gradient Descent) to autonomously optimize its evaluation parameters from historical game data.

## 📊 Performance & Statistics

The engine has been rigorously tested against various versions of [Stockfish](https://github.com/official-stockfish/stockfish) (anchored from 2200 to 3000 Elo) and standard tactical test suites. 

### Playing Strength (Ordo Rating)
Based on a 300-game Round-Robin tournament at `1m+1s` time control:

| Rank | Engine / Player | Ordo Elo | Win Rate | Draw Rate |
|------|-----------------|----------|----------|-----------|
| 1    | Stockfish 3000  | 3000.0   | 92.0%    | 10.7%     |
| 2    | Stockfish 2800  | 2800.0   | 73.0%    | 11.3%     |
| 3    | **MyChess (Tuned)** | **2625.2** | **50.7%**| **10.7%** |
| 4    | **MyChess (Default)**| **2618.5** | **49.8%**| **12.3%** |
| 5    | Stockfish 2600  | 2600.0   | 39.7%    | 10.7%     |
| 6    | Stockfish 2400  | 2400.0   | 28.3%    | 6.7%      |

In a direct 100-game Head-to-Head match, the **Tuned** version defeated the **Default** hand-crafted version **27 - 21** (with 52 draws), proving a definitive Elo gain from the automated learning process.

### Training & Tactical Accuracy
* **[Dataset](https://github.com/KierenP/ChessTrainingSets):** 725,000 `.epd` positions.
* **Epochs:** 1,000 epochs at a learning rate of 1500.
* **Loss (MSE):** Reduced from `0.0648` to `0.0637`.
* **Tactical Vision (WCSAC Test Suite):** Solved **34%** of extremely complex grandmaster tactical puzzles within 3 seconds per move (a 7% accuracy increase over the default weights).

---

## 📖 Engine Architecture Documentation

The core technologies, mathematics, and algorithms behind MyChess are thoroughly documented in our Wiki. If you want to understand how the engine works under the hood, start here:

1. [Core Architecture: Atlas and Magic Bitboards](https://github.com/pojemucha/MyChess/wiki/Core-Architecture:-Atlas-and-Magic-Bitboards)
2. [Core Architecture: Position, Evaluation, and Search](https://github.com/pojemucha/MyChess/wiki/Core-Architecture:-Position,-Evaluation,-and-Search)
3. [Core Architecture: Evaluation Weights and Machine Learning](https://github.com/pojemucha/MyChess/wiki/Core-Architecture:-Evaluation-Weights-and-Machine-Learning)
4. [Core Architecture: UCI and Time Management](https://github.com/pojemucha/MyChess/wiki/Core-Architecture:-Universal-Chess-Interface-(UCI)-and-Time-Management)
5. [Core Architecture: GUI and SFML Integration](https://github.com/pojemucha/MyChess/wiki/Core-Architecture:-Graphical-User-Interface-(GUI)-and-SFML-Integration)

---

## 🛠️ Prerequisites & Building

The project uses CMake to handle building and dependency resolution.

### Requirements
* A modern C++20 compatible compiler (GCC, Clang, or MSVC).
* **CMake** (version 3.24 or higher).
* The **SFML 3.0.0** library (Automatically fetched by CMake during the build process).

### Build Instructions

1. Clone the repository:
   ```bash
   git clone https://github.com/your-username/MyChess.git
   cd MyChess
   ```
2. Create a build directory:
   ```bash
   mkdir build && cd build
   ```
3. Generate the build files and compile the project (Release mode is highly recommended for maximum engine speed):
   ```bash
   cmake .. -DCMAKE_BUILD_TYPE=Release
   cmake --build . --config Release
   ```

This will generate two executables: `MyChess_GUI` and `MyChess_UCI`.

---

## 🚀 Usage

### 1. The Graphical User Interface (`MyChess_GUI`)
Run the `MyChess_GUI` executable to launch the built-in standalone application. 
* **Play a game:** Click "Play" on the main menu, choose your color, and interact directly with the board.
* **Engine Sidebar:** While playing, click the `>` button on the right side of the screen to open the control panel. Here you can adjust the engine's search time, toggle the Principal Variation (PV) arrows, and view real-time evaluation metrics.
* **Train the Engine:** Go to the "Settings" menu. You can provide an absolute path to a `.epd` dataset, adjust the learning rate and epochs, and click **"Train Model"**. The engine will optimize its Piece-Square Tables (PSTs) and save them to `trained_weights.bin`.

### 2. The UCI Console Engine (`MyChess_UCI`)
Run the `MyChess_UCI` executable if you want to plug the engine into professional chess GUIs like **CuteChess**, **Arena**, or **Lichess**.

To install it in your favorite GUI:
1. Open the GUI's engine management settings.
2. Add a new UCI engine.
3. Point the file path to the compiled `MyChess_UCI` executable.
4. The GUI will automatically handle time controls, parameters, and matches!

*Note: The engine will automatically look for `trained_weights.bin` in its current working directory. If it doesn't find it, it will smoothly fall back to its default hand-crafted weights.*
