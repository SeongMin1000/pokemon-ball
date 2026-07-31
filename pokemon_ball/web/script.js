/**
 * ESP32-S3 + TinyML + MQTT AI Pokemon Capsule Dashboard Engine
 * Inspired by GBA Pokemon Battle System
 */

// ==========================================================================
// 1. DATA STRUCTURES & CONFIGURATION
// ==========================================================================
const POKEMON_DATABASE = [
  { id: 25,  name: '피카츄',   nameEn: 'Pikachu',    gesture: 'LEFT',   sprite: 'https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/25.png', hidden: false, color: '#2196f3' },
  { id: 4,   name: '파이리',   nameEn: 'Charmander', gesture: 'RIGHT',  sprite: 'https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/4.png',  hidden: false, color: '#ff9800' },
  { id: 7,   name: '꼬부기',   nameEn: 'Squirtle',   gesture: 'UP',     sprite: 'https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/7.png',  hidden: false, color: '#4caf50' },
  { id: 1,   name: '이상해씨', nameEn: 'Bulbasaur',  gesture: 'DOWN',   sprite: 'https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/1.png',  hidden: false, color: '#9c27b0' },
  { id: 133, name: '이브이',   nameEn: 'Eevee',      gesture: 'CIRCLE', sprite: 'https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/133.png', hidden: false, color: '#e91e63' },
  { id: 999, name: '산지니',   nameEn: 'Sanjini',    gesture: 'CIRCLE', sprite: 'https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/151.png', hidden: true,  color: '#ff4081' }
];

const GESTURE_CONFIG = {
  'LEFT':   { nameKor: '왼쪽',   icon: '⬅️', color: 'var(--color-left)' },
  'RIGHT':  { nameKor: '오른쪽', icon: '➡️', color: 'var(--color-right)' },
  'UP':     { nameKor: '위쪽',   icon: '⬆️', color: 'var(--color-up)' },
  'DOWN':   { nameKor: '아래쪽', icon: '⬇️', color: 'var(--color-down)' },
  'CIRCLE': { nameKor: '원형',   icon: '🔄', color: 'var(--color-circle)' }
};

// State Variables
let mqttClient = null;
let caughtSet = new Set();
let webState = 'IDLE'; // 'IDLE', 'SENSING', 'CATCHING'
let typingTimer = null;
let currentEnemyPokemon = null;
let currentHighestGesture = 'LEFT';
let lastMessageText = '';

// ==========================================================================
// 2. INITIALIZATION
// ==========================================================================
document.addEventListener('DOMContentLoaded', () => {
  loadPokedexData();
  renderPredictionPanel();
  renderPokedexGrid();
  resetBattleArena();

  // Button Listeners
  document.getElementById('btnConnect').addEventListener('click', toggleMqttConnection);
  document.getElementById('btnDemoCapture').addEventListener('click', runDemoCaptureSequence);
  document.getElementById('btnResetDex').addEventListener('click', resetPokedex);

  // Auto connect on page load
  autoConnectMQTT();
});

// ==========================================================================
// 3. TYPEWRITER EFFECT FOR RETRO DIALOG
// ==========================================================================
function typeWriterMessage(htmlText, speed = 20, callback = null) {
  if (lastMessageText === htmlText) return;
  lastMessageText = htmlText;

  const dialogEl = document.getElementById('dialogText');
  if (typingTimer) clearInterval(typingTimer);
  
  dialogEl.innerHTML = '';
  let index = 0;
  let isTag = false;
  let currentHtml = '';

  typingTimer = setInterval(() => {
    if (index >= htmlText.length) {
      clearInterval(typingTimer);
      typingTimer = null;
      if (callback) callback();
      return;
    }

    const char = htmlText.charAt(index);
    if (char === '<') isTag = true;
    currentHtml += char;
    if (char === '>') isTag = false;

    if (!isTag) {
      dialogEl.innerHTML = currentHtml;
    }
    index++;
  }, speed);
}

// ==========================================================================
// 4. IN-ARENA AI PREDICTION PANEL (REAL-TIME CONFIDENCE BARS)
// ==========================================================================
function renderPredictionPanel() {
  const container = document.getElementById('confidenceList');
  container.innerHTML = '';

  Object.keys(GESTURE_CONFIG).forEach(gestKey => {
    const config = GESTURE_CONFIG[gestKey];
    const item = document.createElement('div');
    item.className = 'in-arena-gest-item';
    item.id = `gestItem_${gestKey}`;
    item.innerHTML = `
      <div class="gest-meta-row">
        <span class="gest-label-box">
          <span class="gest-dot" style="background:${config.color}"></span>
          ${gestKey}
        </span>
        <span class="gest-val" id="gestVal_${gestKey}">0%</span>
      </div>
      <div class="mini-progress-track">
        <div class="mini-progress-fill" id="gestFill_${gestKey}" style="background-color:${config.color}; width:0%"></div>
      </div>
    `;
    container.appendChild(item);
  });
}

function updatePredictions(probabilities, activeGesture) {
  if (webState === 'CATCHING') return;

  // Change state to SENSING & start shaking pokeball in trainer's hand!
  if (webState !== 'SENSING') {
    webState = 'SENSING';
    document.getElementById('heldPokeball').classList.add('shaking-ball');
    typeWriterMessage('AI가 제스처를 분석 중...<br><span class="hl-gold">Touch Sensor를 눌러</span> 몬스터볼을 던지세요!');
  }

  let highestGest = 'LEFT';
  let highestProb = -1;

  Object.keys(GESTURE_CONFIG).forEach(gestKey => {
    const val = probabilities[gestKey] || 0;
    const fillEl = document.getElementById(`gestFill_${gestKey}`);
    const valEl = document.getElementById(`gestVal_${gestKey}`);
    const itemEl = document.getElementById(`gestItem_${gestKey}`);

    if (fillEl) fillEl.style.width = `${val}%`;
    if (valEl) valEl.textContent = `${val}%`;
    if (itemEl) itemEl.classList.remove('highlight');

    if (val > highestProb) {
      highestProb = val;
      highestGest = gestKey;
    }
  });

  currentHighestGesture = highestGest;

  // Highlight top confidence item
  const topItem = document.getElementById(`gestItem_${highestGest}`);
  if (topItem) topItem.classList.add('highlight');

  // Update top active gesture badge
  const badgeEl = document.getElementById('activeGestureBadge');
  if (badgeEl) {
    const info = GESTURE_CONFIG[highestGest];
    badgeEl.textContent = `${info.icon} ${highestGest} (${highestProb}%)`;
    badgeEl.style.borderColor = info.color;
  }
}

// ==========================================================================
// 5. POKÉDEX DATA MANAGEMENT
// ==========================================================================
function loadPokedexData() {
  try {
    const stored = localStorage.getItem('pokemon_caught_v2');
    if (stored) {
      caughtSet = new Set(JSON.parse(stored));
    }
  } catch (e) {
    console.error('Failed to load pokedex from localStorage:', e);
  }
}

function savePokedexData() {
  try {
    localStorage.setItem('pokemon_caught_v2', JSON.stringify([...caughtSet]));
  } catch (e) {
    console.error('Failed to save pokedex:', e);
  }
}

function renderPokedexGrid() {
  const grid = document.getElementById('pokedexGrid');
  grid.innerHTML = '';

  POKEMON_DATABASE.forEach(pkmn => {
    const isCaught = caughtSet.has(pkmn.name);
    const card = document.createElement('div');
    card.className = `dex-card ${isCaught ? 'caught' : ''}`;
    card.id = `dexCard_${pkmn.name}`;

    const displayName = pkmn.hidden && !isCaught ? '???' : pkmn.name;
    const spriteUrl = pkmn.hidden && !isCaught ? '' : pkmn.sprite;

    card.innerHTML = `
      <div class="dex-badge">No.${String(pkmn.id).padStart(3, '0')}</div>
      ${spriteUrl ? `<img class="dex-thumb" src="${spriteUrl}" alt="${displayName}">` : `<div style="font-size:2rem; width:48px; height:48px; display:flex; align-items:center; justify-content:center;">❓</div>`}
      <div class="dex-name">${displayName}</div>
    `;

    grid.appendChild(card);
  });
}

function resetPokedex() {
  if (confirm('도감 데이터를 정말 초기화하시겠습니까?')) {
    caughtSet.clear();
    savePokedexData();
    renderPokedexGrid();
    typeWriterMessage('도감이 성공적으로 초기화되었습니다!');
  }
}

// ==========================================================================
// 6. BATTLE ARENA & CAPTURE ANIMATION SEQUENCE
// ==========================================================================
function resetBattleArena() {
  webState = 'IDLE';
  lastMessageText = '';
  
  // Pick random uncaught or default pokemon as wild enemy
  currentEnemyPokemon = POKEMON_DATABASE[Math.floor(Math.random() * (POKEMON_DATABASE.length - 1))];

  const silhouetteImg = document.getElementById('enemySilhouetteImg');
  const actualImg = document.getElementById('enemyActualImg');
  const heldPokeball = document.getElementById('heldPokeball');
  const flyingBall = document.getElementById('pokeballFlying');

  // Reset Enemy Layers: Silhouette ON, Actual OFF
  silhouetteImg.src = currentEnemyPokemon.sprite;
  silhouetteImg.className = 'pkm-layer pkm-silhouette';
  actualImg.src = currentEnemyPokemon.sprite;
  actualImg.className = 'pkm-layer pkm-actual';

  // Held Pokeball still
  heldPokeball.classList.remove('shaking-ball');
  heldPokeball.style.opacity = '1';

  // Hide flying ball
  flyingBall.className = 'pokeball-flying';
  flyingBall.style.opacity = '0';

  typeWriterMessage('보드를 흔들어 포켓몬을 탐색하세요.');
}

function triggerCaptureSequence(targetPokemonName, gestureName, isHidden = false) {
  if (webState === 'CATCHING') return;
  webState = 'CATCHING';

  // Stop held pokeball shaking animation
  const heldPokeball = document.getElementById('heldPokeball');
  heldPokeball.classList.remove('shaking-ball');
  heldPokeball.style.opacity = '0';

  // Determine target pokemon
  let targetPkmn = POKEMON_DATABASE.find(p => p.name === targetPokemonName || p.nameEn === targetPokemonName);
  if (!targetPkmn) {
    targetPkmn = currentEnemyPokemon || POKEMON_DATABASE[0];
  }
  currentEnemyPokemon = targetPkmn;

  const trainerSprite = document.getElementById('trainerSprite');
  const silhouetteImg = document.getElementById('enemySilhouetteImg');
  const actualImg = document.getElementById('enemyActualImg');
  const flyingBall = document.getElementById('pokeballFlying');
  const flashScreen = document.getElementById('captureFlash');

  // Step 1: Typewriter message
  typeWriterMessage('가랏! <span class="hl-red">몬스터볼!</span>');

  // Step 2: Trainer throw pose
  trainerSprite.classList.add('throw-pose');
  setTimeout(() => trainerSprite.classList.remove('throw-pose'), 400);

  // Position dynamic ball trajectory starting directly from trainer's hand to pokemon center
  const heldRect = heldPokeball.getBoundingClientRect();
  const enemyRect = silhouetteImg.getBoundingClientRect();
  const stageRect = document.getElementById('battleArenaView').getBoundingClientRect();

  const startX = (heldRect.left - stageRect.left) + (heldRect.width * 0.5) - 18;
  const startY = (heldRect.top - stageRect.top) + (heldRect.height * 0.5) - 18;

  const targetX = (enemyRect.left - stageRect.left) + (enemyRect.width * 0.5) - startX - 18;
  const targetY = (enemyRect.top - stageRect.top) + (enemyRect.height * 0.5) - startY - 18;

  flyingBall.style.left = `${startX}px`;
  flyingBall.style.top = `${startY}px`;
  flyingBall.style.setProperty('--target-x', `${targetX}px`);
  flyingBall.style.setProperty('--target-y', `${targetY}px`);
  flyingBall.style.setProperty('--target-x-half', `${targetX * 0.5}px`);
  flyingBall.style.setProperty('--target-y-peak', `${targetY - 140}px`);

  // Step 3: Parabolic throw arc
  flyingBall.className = 'pokeball-flying anim-throw';

  // Step 4: Ball lands & 1~3 times shake animation
  setTimeout(() => {
    flyingBall.className = 'pokeball-flying anim-shake';
    typeWriterMessage('...');
  }, 750);

  // Step 5: Capture Success & Smooth Silhouette Cross-Fade to Actual Image
  setTimeout(() => {
    // White Flash Burst
    flashScreen.classList.remove('active');
    void flashScreen.offsetWidth;
    flashScreen.classList.add('active');

    // Hide flying ball
    flyingBall.style.opacity = '0';

    // Cross-fade Silhouette -> Actual Color Pokemon Image
    silhouetteImg.classList.add('fade-out');
    actualImg.classList.add('fade-in');

    // Message
    typeWriterMessage(`잡았다!<br><span class="hl-gold">${targetPkmn.name}</span>(을)를 획득했다!`);

    // Step 6: Pokedex Auto Unlock
    caughtSet.add(targetPkmn.name);
    savePokedexData();
    renderPokedexGrid();

    // Unlock bounce animation on pokedex card
    const dexCard = document.getElementById(`dexCard_${targetPkmn.name}`);
    if (dexCard) {
      dexCard.classList.add('unlock-anim');
      setTimeout(() => dexCard.classList.remove('unlock-anim'), 800);
    }
  }, 2100);

  // Step 7: Reset to next encounter after delay
  setTimeout(() => {
    resetBattleArena();
  }, 6500);
}

// ==========================================================================
// 7. DEMO / SIMULATION MODE (For Browser Testing)
// ==========================================================================
function runDemoCaptureSequence() {
  const gestures = ['LEFT', 'RIGHT', 'UP', 'DOWN', 'CIRCLE'];
  const targetGest = gestures[Math.floor(Math.random() * gestures.length)];
  
  const mockProbs = {};
  gestures.forEach(g => mockProbs[g] = Math.floor(Math.random() * 20));
  mockProbs[targetGest] = 86;

  updatePredictions(mockProbs, targetGest);

  const matchedPkmn = POKEMON_DATABASE.find(p => p.gesture === targetGest) || POKEMON_DATABASE[0];
  
  setTimeout(() => {
    triggerCaptureSequence(matchedPkmn.name, targetGest, matchedPkmn.hidden);
  }, 600);
}

// ==========================================================================
// 8. MQTT WEBSOCKET CONNECTION MANAGER
// ==========================================================================
function autoConnectMQTT() {
  toggleMqttConnection(true);
}

function toggleMqttConnection(auto = false) {
  if (mqttClient && mqttClient.connected) {
    mqttClient.end();
    setMqttStatus(false);
    return;
  }

  const host = document.getElementById('mqttHost').value.trim() || 'localhost';
  const port = parseInt(document.getElementById('mqttPort').value.trim()) || 9001;

  const clientUri = `ws://${host}:${port}`;
  console.log(`Connecting to MQTT WebSocket at ${clientUri}...`);

  try {
    mqttClient = mqtt.connect(clientUri, {
      clientId: `pkb_dash_${Math.random().toString(16).substr(2, 6)}`,
      clean: true,
      connectTimeout: 4000
    });

    mqttClient.on('connect', () => {
      console.log('MQTT Connected successfully!');
      setMqttStatus(true);
      mqttClient.subscribe(['pokemon/predict', 'pokemon/capture', 'pokemon/config']);
    });

    mqttClient.on('message', (topic, payload) => {
      try {
        const data = JSON.parse(payload.toString());
        handleMqttData(topic, data);
      } catch (err) {
        console.error('MQTT JSON Parse Error:', err);
      }
    });

    mqttClient.on('close', () => setMqttStatus(false));
    mqttClient.on('error', (err) => {
      console.error('MQTT Error:', err);
      setMqttStatus(false);
    });

  } catch (err) {
    console.error('Failed to create MQTT client:', err);
    setMqttStatus(false);
  }
}

function setMqttStatus(isConnected) {
  const dot = document.getElementById('mqttStatusDot');
  const text = document.getElementById('mqttStatusText');
  const btn = document.getElementById('btnConnect');

  if (isConnected) {
    dot.classList.add('connected');
    text.textContent = 'MQTT 온라인';
    btn.textContent = '해제';
    btn.style.background = 'linear-gradient(to bottom, #4caf50, #2e7d32)';
  } else {
    dot.classList.remove('connected');
    text.textContent = '오프라인';
    btn.textContent = '연결';
    btn.style.background = 'linear-gradient(to bottom, #ff5252, #c62828)';
  }
}

function handleMqttData(topic, data) {
  if (topic === 'pokemon/predict' || data.type === 'prediction') {
    updatePredictions(data.probabilities || {}, data.gesture);
  } else if (topic === 'pokemon/capture' || data.type === 'capture') {
    triggerCaptureSequence(data.pokemon, data.gesture, data.hidden);
  }
}
