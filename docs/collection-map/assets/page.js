const MAP = JSON.parse(document.getElementById('data').textContent);
const graph = document.getElementById('graph');
const panel = document.getElementById('panel');
const nodes = [...graph.querySelectorAll('.node')];
const links = [...graph.querySelectorAll('.link')];
const ties = [...graph.querySelectorAll('.tie')];

const parallel = {};
for (const [name, c] of Object.entries(MAP.collections))
  if (c.companion_of) (parallel[c.companion_of] ||= []).push(name);

// Where a collection's values came from: read out of a DST bank, recomputed
// by SKELANA at conversion time, or produced by this converter.
const PROVENANCE = {
  transcribed: 'transcribed (DST bank)',
  derived: 'derived (SKELANA)',
  custom: 'custom (converter)',
};

// Conventions the schema cannot carry -- a slot layout, a bit word, a coded
// integer -- declared as an enum beside the writer and rendered here. This is
// the only place a reader of the file can learn what these numbers mean.
const HEAD = { index: '#', mask: 'mask', value: 'value' };

const docTable = (t) =>
  '<h3>' + t.title + '</h3>'
  + '<table class="slots"><thead><tr><th>' + (HEAD[t.kind] || '#') + '</th>'
  + '<th>name</th><th>meaning</th></tr></thead><tbody>'
  + t.rows.map(r => '<tr><td><code>' + r.label + '</code></td>'
      + '<td><code>' + r.name + '</code></td><td>' + r.brief
      + (r.bits ? '<table class="bits"><tbody>'
          + r.bits.map(b => '<tr><td><code>' + b.label + '</code></td>'
              + '<td><code>' + b.name + '</code></td><td>' + b.brief
              + '</td></tr>').join('')
          + '</tbody></table>' : '')
      + '</td></tr>').join('')
  + '</tbody></table>';

const list = (items) => items.length
  ? '<ul>' + items.map(i => '<li>' + i + '</li>').join('') + '</ul>'
  : '<span class="no">none</span>';

function describe(name) {
  if (name === 'podio::Frame') {
    const keys = Object.keys(MAP.parameters);
    return '<h2>podio::Frame</h2><p>Frame parameters, not a collection.</p>'
         + '<dl><dt>Parameters (' + keys.length + ')</dt><dd>'
         + list(keys) + '</dd></dl>';
  }
  const c = MAP.collections[name];
  const out = MAP.links.filter(l => l.from === name)
                       .map(l => l.relation + ' &rarr; ' + l.to);
  const inb = MAP.links.filter(l => l.to === name)
                       .map(l => l.from + ' &rarr; ' + l.relation);
  return '<h2>' + name + '</h2>'
    + '<p><span class="tag">' + c.domain + '</span> '
    + '<span class="tag">' + (PROVENANCE[c.provenance] || c.provenance)
    + '</span></p>'
    + '<dl>'
    + '<dt>Type</dt><dd>' + c.type + '</dd>'
    + '<dt>Populated in</dt><dd>' + list(c.populated_in) + '</dd>'
    + '<dt>Links out</dt><dd>' + list(out) + '</dd>'
    + '<dt>Links in</dt><dd>' + list(inb) + '</dd>'
    + '<dt>Parallel arrays</dt><dd>' + list(parallel[name] || []) + '</dd>'
    + '<dt>Unfilled relations</dt><dd>' + list(c.unfilled_relations) + '</dd>'
    + '</dl>'
    + (c.tables || []).map(docTable).join('');
}

function focus(name) {
  graph.classList.toggle('focused', !!name);
  if (!name) {
    nodes.forEach(n => n.classList.remove('lit'));
    links.forEach(l => l.classList.remove('lit'));
    ties.forEach(t => t.classList.remove('lit'));
    panel.innerHTML = INTRO;
    return;
  }
  const near = new Set([name]);
  MAP.links.forEach(l => {
    if (l.from === name) near.add(l.to);
    if (l.to === name) near.add(l.from);
  });
  nodes.forEach(n => n.classList.toggle('lit', near.has(n.dataset.name)));
  links.forEach(l => l.classList.toggle('lit',
    l.dataset.from === name || l.dataset.to === name));
  ties.forEach(t => {
    const on = t.dataset.name === name || t.dataset.parent === name;
    t.classList.toggle('lit', on);
    if (on) { near.add(t.dataset.name); near.add(t.dataset.parent); }
  });
  nodes.forEach(n => n.classList.toggle('lit', near.has(n.dataset.name)));
  panel.innerHTML = describe(name);
}

nodes.forEach(n => n.addEventListener('click', () => focus(n.dataset.name)));
graph.addEventListener('click', e => { if (e.target === graph) focus(null); });

document.getElementById('find').addEventListener('input', e => {
  const q = e.target.value.trim().toLowerCase();
  graph.classList.toggle('searching', !!q);
  nodes.forEach(n => {
    const name = n.dataset.name;
    const c = MAP.collections[name];
    const hay = [name, c ? c.type : '', c ? c.bank : '',
                 ...(parallel[name] || []),
                 name === 'podio::Frame'
                   ? Object.keys(MAP.parameters).join(' ') : ''
                ].join(' ').toLowerCase();
    n.classList.toggle('hit', hay.includes(q));
  });
});

function filter() {
  const off = new Set([...document.querySelectorAll('.prov:not(:checked)')]
                      .map(c => c.value));
  const gone = new Set();
  nodes.forEach(n => {
    const c = MAP.collections[n.dataset.name];
    const drop = !!c && off.has(c.provenance);
    n.classList.toggle('off', drop);
    if (drop) gone.add(n.dataset.name);
  });
  links.forEach(l => l.classList.toggle('off',
    gone.has(l.dataset.from) || gone.has(l.dataset.to)));
  ties.forEach(t => t.classList.toggle('off',
    gone.has(t.dataset.name) || gone.has(t.dataset.parent)));
}

document.querySelectorAll('.prov').forEach(c =>
  c.addEventListener('change', filter));

document.getElementById('show-companions').addEventListener('change', e => {
  document.body.classList.toggle('no-companions', !e.target.checked);
});

document.querySelectorAll('.key').forEach(key =>
  key.addEventListener('click', () => {
    const domain = key.dataset.domain;
    const on = !key.classList.contains('active');
    document.querySelectorAll('.key').forEach(k => k.classList.remove('active'));
    key.classList.toggle('active', on);
    graph.classList.toggle('searching', on);
    nodes.forEach(n => n.classList.toggle('hit', on && n.dataset.domain === domain));
  }));
