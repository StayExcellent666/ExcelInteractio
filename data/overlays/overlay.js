(() => {
  'use strict';
  const canvas = document.querySelector('canvas'), ctx = canvas.getContext('2d');
  const effect = document.body.dataset.effect;
  const width = 960, height = 540; canvas.width = width; canvas.height = height;
  const colors = ['#70e8ff', '#af82ff', '#ff7ebf', '#ffd46c', '#83f5c0'];
  const random = (a, b) => a + Math.random() * (b - a);
  const particles = Array.from({length: effect === 'confetti' ? 90 : 38}, () => ({
    x: random(0, width), y: random(-height, height), r: random(2, 6), speed: random(45, 135),
    phase: random(0, Math.PI * 2), angle: random(0, Math.PI * 2), color: colors[Math.floor(random(0, colors.length))]
  }));
  function border(t) {
    const alpha = .65 + .25 * Math.sin(t * 2.5);
    const gradient = ctx.createLinearGradient(0, 0, width, height);
    gradient.addColorStop(0, '#58e1ff'); gradient.addColorStop(.5, '#ae7eff'); gradient.addColorStop(1, '#ff82cb');
    ctx.globalAlpha = alpha; ctx.strokeStyle = gradient; ctx.lineWidth = 5;
    ctx.shadowColor = '#70dfff'; ctx.shadowBlur = 18; ctx.strokeRect(10, 10, width - 20, height - 20);
    ctx.shadowBlur = 0; ctx.globalAlpha = .8; ctx.lineWidth = 2; ctx.strokeRect(18, 18, width - 36, height - 36);
  }
  function sparkle(x, y, r, color, alpha) {
    ctx.save(); ctx.translate(x, y); ctx.globalAlpha = alpha; ctx.fillStyle = color;
    ctx.shadowColor = color; ctx.shadowBlur = r * 3; ctx.beginPath();
    ctx.moveTo(0, -r * 2); ctx.quadraticCurveTo(r * .3, -r * .3, r * 2, 0);
    ctx.quadraticCurveTo(r * .3, r * .3, 0, r * 2); ctx.quadraticCurveTo(-r * .3, r * .3, -r * 2, 0);
    ctx.quadraticCurveTo(-r * .3, -r * .3, 0, -r * 2); ctx.fill(); ctx.restore();
  }
  function frame(ms) {
    const t = ms / 1000; ctx.clearRect(0, 0, width, height); ctx.save();
    if (effect === 'neon') { border(t); }
    else if (effect === 'confetti') {
      for (const p of particles) {
        const x = (p.x + Math.sin(t * 1.8 + p.phase) * 32 + width) % width;
        const y = ((p.y + t * p.speed) % (height + 80) + height + 80) % (height + 80) - 40;
        ctx.save(); ctx.translate(x, y); ctx.rotate(p.angle + t * 2); ctx.globalAlpha = .92;
        ctx.fillStyle = p.color; ctx.fillRect(-p.r, -p.r * .45, p.r * 2, p.r * .9); ctx.restore();
      }
    } else if (effect === 'sparks') {
      for (const p of particles) sparkle(p.x, p.y, p.r, p.color, Math.max(0, Math.sin(t * 2 + p.phase)) * .85);
    } else if (effect === 'glitch') {
      const beat = Math.floor(t * 7); if (beat % 11 < 3) {
        ctx.globalAlpha = .7; ctx.fillStyle = '#69e7ff';
        for (let i = 0; i < 7; i++) { const y = (i * 93 + beat * 31) % height; ctx.fillRect((i * 157 + beat * 43) % width, y, 70 + (i * 39) % 250, 2 + i % 5); }
        ctx.strokeStyle = '#ff74cc'; ctx.lineWidth = 4; ctx.strokeRect(7, 7, width - 14, height - 14);
      }
    }
    ctx.restore(); requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
})();
