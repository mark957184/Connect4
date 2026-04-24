// Connect Four AI - Clean WebAssembly Interface
// Game state managed in JavaScript, AI calculations in WASM

const ROWS = 6;
const COLS = 7;

// Game state managed in JavaScript
class Game {
    constructor() {
        this.pos1 = 0n;
        this.pos2 = 0n;
        this.mask = 0n;
        this.moves = 0;
        this.gameOver = false;
        this.winner = 0;
    }

    p1Turn() {
        return this.moves % 2 === 0;
    }

    curPos() {
        return this.p1Turn() ? this.pos1 : this.pos2;
    }

    canPlay(col) {
        if (col < 0 || col >= COLS) return false;
        const H = ROWS + 1;
        const top = 1n << BigInt((ROWS - 1) + col * H);
        return (this.mask & top) === 0n;
    }

    play(col) {
        if (!this.canPlay(col)) return false;
        
        const H = ROWS + 1;
        const bot = 1n << BigInt(col * H);
        const cmask = ((1n << BigInt(ROWS)) - 1n) << BigInt(col * H);
        const np = (this.mask + bot) & cmask;
        
        const wasP1Turn = this.p1Turn();
        if (wasP1Turn) {
            this.pos1 |= np;
        } else {
            this.pos2 |= np;
        }
        this.mask |= np;
        this.moves++;
        
        // Check for win - use the position of the player who just moved
        const curPos = wasP1Turn ? this.pos1 : this.pos2;
        if (this.hasWon(curPos)) {
            this.gameOver = true;
            this.winner = wasP1Turn ? 1 : 2;
            return true;
        }
        
        if (this.moves >= ROWS * COLS) {
            this.gameOver = true;
            this.winner = 3;
        }
        
        return false;
    }

    hasWon(pos) {
        const H = BigInt(ROWS + 1);
        let m;
        m = pos & (pos >> H);
        if (m & (m >> (2n * H))) return true;
        m = pos & (pos >> (H + 1n));
        if (m & (m >> (2n * (H + 1n)))) return true;
        m = pos & (pos >> (H - 1n));
        if (m & (m >> (2n * (H - 1n)))) return true;
        m = pos & (pos >> 1n);
        if (m & (m >> 2n)) return true;
        return false;
    }

    cell(row, col) {
        const H = ROWS + 1;
        const bit = 1n << BigInt(col * H + (ROWS - 1 - row));
        if (this.pos1 & bit) return 1;
        if (this.pos2 & bit) return 2;
        return 0;
    }

    reset() {
        this.pos1 = 0n;
        this.pos2 = 0n;
        this.mask = 0n;
        this.moves = 0;
        this.gameOver = false;
        this.winner = 0;
    }
}

// AI wrapper using WASM
class ConnectFourAI {
    constructor() {
        this.initialized = false;
        this.init_ai = null;
        this.load_book = null;
        this.load_cache = null;
        this.save_cache = null;
        this.reset_game = null;
        this.play_move = null;
        this.get_cell = null;
        this.is_game_over = null;
        this.get_winner = null;
        this.get_best_move = null;
    }

    async init() {
        try {
            // Emscripten creates a global Module object, we need to wait for it to be ready
            if (typeof Module === 'undefined') {
                console.error('Module not loaded');
                return;
            }
            
            // Wait for Module to be ready
            await new Promise((resolve) => {
                if (Module.calledRun) {
                    resolve();
                } else {
                    Module.onRuntimeInitialized = resolve;
                }
            });
            
            this.init_ai = Module.cwrap('init_ai', 'void', []);
            this.load_book = Module.cwrap('load_book', 'number', ['string']);
            this.load_book_from_memory = Module.cwrap('load_book_from_memory', 'number', ['number', 'number']);
            this.load_cache = Module.cwrap('load_cache', 'number', ['string']);
            this.load_cache_from_memory = Module.cwrap('load_cache_from_memory', 'number', ['number', 'number']);
            this.copy_to_memory = Module.cwrap('copy_to_memory', 'void', ['number', 'number', 'number']);
            this.save_cache = Module.cwrap('save_cache', 'number', ['string']);
            this.reset_game = Module.cwrap('reset_game', 'void', []);
            this.play_move = Module.cwrap('play_move', 'number', ['number']);
            this.get_cell = Module.cwrap('get_cell', 'number', ['number', 'number']);
            this.is_game_over = Module.cwrap('is_game_over', 'number', []);
            this.get_winner = Module.cwrap('get_winner', 'number', []);
            this.get_best_move = Module.cwrap('get_best_move', 'number', ['number', 'number', 'number', 'number', 'number', 'number', 'number']);
            this.malloc = Module.cwrap('malloc', 'number', ['number']);
            this.free = Module.cwrap('free', 'void', ['number']);
            
            this.init_ai();
            
            // Load book and cache from local files (now in repository with LFS)
            try {
                const bookResponse = await fetch('7x6.book');
                if (bookResponse.ok) {
                    const bookData = await bookResponse.arrayBuffer();
                    const bookDataArray = new Uint8Array(bookData);
                    const bookPtr = this.malloc(bookDataArray.length);
                    // Direct byte-by-byte copy using HEAP8
                    const heap8 = new Int8Array(Module.HEAP8.buffer);
                    for (let i = 0; i < bookDataArray.length; i++) {
                        heap8[bookPtr + i] = bookDataArray[i];
                    }
                    const bookLoaded = this.load_book_from_memory(bookPtr, bookDataArray.length);
                    this.free(bookPtr);
                    console.log('Book loaded:', bookLoaded ? 'success' : 'failed');
                } else {
                    console.log('Book file not found');
                }
            } catch (e) {
                console.log('Failed to load book:', e);
            }
            
            try {
                const cacheResponse = await fetch('opening_override.bin');
                if (cacheResponse.ok) {
                    const cacheData = await cacheResponse.arrayBuffer();
                    const cacheDataArray = new Uint8Array(cacheData);
                    const cachePtr = this.malloc(cacheDataArray.length);
                    // Direct byte-by-byte copy using HEAP8
                    const heap8 = new Int8Array(Module.HEAP8.buffer);
                    for (let i = 0; i < cacheDataArray.length; i++) {
                        heap8[cachePtr + i] = cacheDataArray[i];
                    }
                    const cacheLoaded = this.load_cache_from_memory(cachePtr, cacheDataArray.length);
                    this.free(cachePtr);
                    console.log('Cache loaded:', cacheLoaded ? 'success' : 'failed');
                } else {
                    console.log('Cache file not found');
                }
            } catch (e) {
                console.log('Failed to load cache:', e);
            }
            
            this.initialized = true;
        } catch (error) {
            console.error('Failed to initialize WASM:', error);
        }
    }

    getBestMove(game) {
        if (!this.initialized) {
            console.error('AI not initialized');
            return 3;
        }
        
        // Split 64-bit BigInt into low/high 32-bit parts
        const pos1Low = Number(game.pos1 & 0xFFFFFFFFn);
        const pos1High = Number((game.pos1 >> 32n) & 0xFFFFFFFFn);
        const pos2Low = Number(game.pos2 & 0xFFFFFFFFn);
        const pos2High = Number((game.pos2 >> 32n) & 0xFFFFFFFFn);
        const maskLow = Number(game.mask & 0xFFFFFFFFn);
        const maskHigh = Number((game.mask >> 32n) & 0xFFFFFFFFn);
        const moves = game.moves;
        
        return this.get_best_move(pos1Low, pos1High, pos2Low, pos2High, maskLow, maskHigh, moves);
    }
}

// UI Controller
class GameController {
    constructor() {
        this.game = new Game();
        this.ai = new ConnectFourAI();
        this.humanIsP1 = true;
        this.gameOver = false;
        
        this.initUI();
        
        // Initialize WASM AI
        this.ai.init().then(() => {
            document.getElementById('loading').classList.add('hidden');
            console.log('WASM AI initialized');
        }).catch((error) => {
            console.error('WASM initialization failed:', error);
            document.getElementById('loading').classList.add('hidden');
        });
    }

    initUI() {
        this.board = document.getElementById('board');
        this.columnButtons = document.getElementById('column-buttons');
        this.turnInfo = document.getElementById('turn-info');
        this.newGameBtn = document.getElementById('new-game-btn');
        this.orderBtn = document.getElementById('order-btn');
        
        this.createBoard();
        this.createColumnButtons();
        
        this.newGameBtn.addEventListener('click', () => this.newGame());
        this.orderBtn.addEventListener('click', () => this.toggleOrder());
    }

    createBoard() {
        this.board.innerHTML = '';
        for (let r = 0; r < ROWS; r++) {
            for (let c = 0; c < COLS; c++) {
                const cell = document.createElement('div');
                cell.className = 'cell empty';
                cell.dataset.row = r;
                cell.dataset.col = c;
                cell.addEventListener('click', () => this.handleCellClick(c));
                this.board.appendChild(cell);
            }
        }
    }

    createColumnButtons() {
        this.columnButtons.innerHTML = '';
        for (let c = 0; c < COLS; c++) {
            const btn = document.createElement('button');
            btn.className = 'column-btn';
            btn.textContent = c + 1;
            btn.addEventListener('click', () => this.handleCellClick(c));
            this.columnButtons.appendChild(btn);
        }
    }

    handleCellClick(col) {
        if (this.game.gameOver) return;
        
        const humanTurn = (this.game.moves % 2 === 0) === this.humanIsP1;
        if (!humanTurn) return;
        
        if (!this.game.canPlay(col)) return;
        
        this.makeMove(col);
        
        if (!this.game.gameOver) {
            setTimeout(() => this.aiMove(), 100);
        }
    }

    makeMove(col) {
        const won = this.game.play(col);
        this.updateBoard();
        
        if (this.game.gameOver) {
            if (this.game.winner === 1) {
                // Player 1 won
                const winner = this.humanIsP1 ? 'You' : 'AI';
                this.turnInfo.textContent = `${winner} win!`;
            } else if (this.game.winner === 2) {
                // Player 2 won
                const winner = this.humanIsP1 ? 'AI' : 'You';
                this.turnInfo.textContent = `${winner} win!`;
            } else {
                this.turnInfo.textContent = 'Draw!';
            }
        } else {
            const humanTurn = (this.game.moves % 2 === 0) === this.humanIsP1;
            this.turnInfo.textContent = humanTurn ? 'Your turn' : 'AI thinking...';
        }
    }

    aiMove() {
        if (this.game.gameOver) return;
        
        const col = this.ai.getBestMove(this.game);
        this.makeMove(col);
    }

    updateBoard() {
        const cells = this.board.querySelectorAll('.cell');
        for (let r = 0; r < ROWS; r++) {
            for (let c = 0; c < COLS; c++) {
                const cell = cells[r * COLS + c];
                const value = this.game.cell(r, c);
                cell.className = 'cell';
                cell.textContent = '';
                if (value === 0) {
                    cell.classList.add('empty');
                } else if (value === 1) {
                    cell.classList.add('player1');
                    cell.textContent = 'O';
                } else {
                    cell.classList.add('player2');
                    cell.textContent = 'X';
                }
            }
        }
    }

    newGame() {
        this.game.reset();
        this.updateBoard();
        this.turnInfo.textContent = this.humanIsP1 ? 'Your turn' : 'AI thinking...';
        
        if (!this.humanIsP1) {
            setTimeout(() => this.aiMove(), 100);
        }
    }

    toggleOrder() {
        this.humanIsP1 = !this.humanIsP1;
        this.orderBtn.textContent = this.humanIsP1 ? 'Go Second' : 'Go First';
        this.newGame();
    }
}

// Initialize game when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    new GameController();
});
