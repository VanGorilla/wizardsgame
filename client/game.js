const boardElement = document.querySelector('#board');
const feedbackElement = document.querySelector('#feedback');
const directionButtons = [...document.querySelectorAll('[data-direction]')];
let state = null;
let selectedDirection = 'up';

function currentPlayer() {
  return state.players[state.currentPlayer];
}

function escapeHtml(text) {
  const element = document.createElement('span');
  element.textContent = text;
  return element.innerHTML;
}

function render() {
  if (!state) return;
  const active = currentPlayer();
  const isOver = state.winner >= 0;
  document.querySelector('#turn-marker').textContent = isOver ? 'DUEL COMPLETE' : `${active.name.toUpperCase()} TO MOVE`;
  document.querySelector('#active-name').textContent = active.name;
  document.querySelector('#active-health').innerHTML = Array.from({ length: 3 }, (_, index) =>
    `<i class="health-pip ${index < active.hp ? 'filled' : ''}"></i>`).join('');
  document.querySelector('#active-mana').textContent = active.mana ? 'Ready' : 'Spent';
  document.querySelector('#active-actions').textContent = `${state.actionsLeft} / 3`;

  boardElement.innerHTML = '';
  for (let y = 0; y < state.map.length; y++) {
    for (let x = 0; x < state.map[y].length; x++) {
      const tile = document.createElement('button');
      const isWall = state.map[y][x] === '#';
      const playerIndex = state.players.findIndex((player) => player.x === x && player.y === y);
      const isHome = state.players.some((player) => player.homeX === x && player.homeY === y);
      const relic = state.relics.find((item) => item.available && item.x === x && item.y === y);
      tile.type = 'button';
      tile.className = `tile ${isWall ? 'wall' : 'floor'} ${isHome ? 'home' : ''} ${!isWall && !isOver ? 'current' : ''}`;
      tile.setAttribute('role', 'gridcell');
      tile.setAttribute('aria-label', `${isWall ? 'Wall' : 'Floor'} at ${x + 1}, ${y + 1}${playerIndex >= 0 ? `, ${state.players[playerIndex].name}` : ''}${relic ? ', relic' : ''}`);
      tile.disabled = isWall || isOver;
      if (relic) tile.insertAdjacentHTML('beforeend', '<span class="relic-mark" aria-hidden="true">✦</span>');
      if (playerIndex >= 0) {
        const wizard = document.createElement('span');
        wizard.className = `wizard ${playerIndex === 0 ? 'aster' : 'bramble'}`;
        wizard.textContent = playerIndex === 0 ? 'A' : 'B';
        wizard.setAttribute('aria-hidden', 'true');
        tile.append(wizard);
      }
      tile.addEventListener('click', () => moveTo(x, y));
      boardElement.append(tile);
    }
  }

  document.querySelector('#player-list').innerHTML = state.players.map((player, index) => `
    <div class="score-row ${index === state.currentPlayer && !isOver ? 'active' : ''}">
      <span>${player.name}</span><small>${player.carried} carried</small><span class="score-value">${player.score} / 2</span>
    </div>`).join('');
  document.querySelector('#event-log').innerHTML = [...state.log].reverse().map((entry) => `<li>${escapeHtml(entry)}</li>`).join('');
  document.querySelector('#move-action').disabled = isOver || state.actionsLeft < 1;
  document.querySelector('#cast-action').disabled = isOver || state.actionsLeft < 1 || active.mana < 1;
  document.querySelector('#collect-action').disabled = isOver || state.actionsLeft < 1 || active.carried >= 2;
  document.querySelector('#deliver-action').disabled = isOver || state.actionsLeft < 1 || active.carried < 1 || active.x !== active.homeX || active.y !== active.homeY;
  document.querySelector('#end-turn').disabled = isOver;
  directionButtons.forEach((button) => button.classList.toggle('selected', button.dataset.direction === selectedDirection));

  const dialog = document.querySelector('#winner-dialog');
  if (isOver && !dialog.open) {
    document.querySelector('#winner-title').textContent = `${state.players[state.winner].name} prevails`;
    dialog.showModal();
  } else if (!isOver && dialog.open) {
    dialog.close();
  }
}

async function request(path, options = {}) {
  feedbackElement.textContent = '';
  try {
    const response = await fetch(path, { ...options, headers: { 'Content-Type': 'application/json', ...options.headers } });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || 'The action could not be completed.');
    state = result;
    render();
  } catch (error) {
    feedbackElement.textContent = error.message || 'Could not reach the game server.';
  }
}

async function act(type, extra = {}) {
  await request('/api/action', { method: 'POST', body: JSON.stringify({ type, ...extra }) });
}

function moveTo(x, y) {
  if (!state || state.winner >= 0) return;
  const player = currentPlayer();
  const dx = x - player.x;
  const dy = y - player.y;
  if (Math.abs(dx) + Math.abs(dy) !== 1) return;
  const direction = dx === 1 ? 'right' : dx === -1 ? 'left' : dy === 1 ? 'down' : 'up';
  act('move', { direction });
}

directionButtons.forEach((button) => button.addEventListener('click', () => {
  selectedDirection = button.dataset.direction;
  directionButtons.forEach((item) => item.classList.toggle('selected', item === button));
}));
document.querySelector('#move-action').addEventListener('click', () => act('move', { direction: selectedDirection }));
document.querySelector('#cast-action').addEventListener('click', () => act('cast', { direction: selectedDirection }));
document.querySelector('#collect-action').addEventListener('click', () => act('collect'));
document.querySelector('#deliver-action').addEventListener('click', () => act('deliver'));
document.querySelector('#end-turn').addEventListener('click', () => act('end'));
document.querySelector('#new-game').addEventListener('click', () => request('/api/reset', { method: 'POST' }));
document.querySelector('#play-again').addEventListener('click', () => {
  document.querySelector('#winner-dialog').close();
  request('/api/reset', { method: 'POST' });
});

request('/api/state');