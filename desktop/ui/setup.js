const $ = id => document.getElementById(id);
const steps = ['welcome', 'source', 'prepare', 'ready'];
const bytes = value => new Intl.NumberFormat(undefined, { maximumFractionDigits: 1 }).format(value / 1048576) + ' MB';

export function initFirstMacSetup(api, finish) {
  let active = false, step = 0, busy = false, loading = false, paused = false, entries = [], readyID, cdChosen = false;
  const source = () => document.querySelector('input[name="setup-source"]:checked').value;
  const entry = () => entries.find(row => row.id === $('setup-edition').value);
  const validName = () => {
    const name = $('setup-name').value.trim();
    return name.length > 0 && name.length <= 100 && !/[\x00-\x1f/\\:]/.test(name) && name !== '.' && name !== '..';
  };
  function render() {
    $('setup').hidden = !active;
    document.querySelector('.workspace').hidden = active;
    document.querySelector('.window').classList.toggle('setting-up', active);
    for (const panel of document.querySelectorAll('#setup [data-step]')) panel.hidden = panel.dataset.step !== steps[step];
    $('setup-title').textContent = ['Welcome to ClassicMac', 'How would you like to start?', source() === 'download' ? 'Choose your first Mac' : 'Bring your own Mac OS', source() === 'installer' ? 'Your Mac is ready to install' : 'Your Mac is ready'][step];
    $('setup-step').textContent = `Step ${step + 1} of 4 · Your first classic Mac`;
    $('setup-back').hidden = step === 0 || step === 3;
    $('setup-back').disabled = busy || loading;
    $('setup-later').hidden = step !== 3;
    $('setup-next').textContent = step < 2 ? 'Continue' : step === 3 ? (source() === 'installer' ? 'Start Installer' : 'Start Mac') : source() === 'download' ? (paused ? 'Resume Download' : 'Download Mac') : 'Create Mac';
    $('setup-next').disabled = busy || loading || (step === 0 && !$('setup-license').checked) || (step === 2 && (!validName() || (source() === 'download' ? !entry() : !cdChosen)));
    $('setup-download-fields').hidden = source() !== 'download';
    $('setup-installer-fields').hidden = source() !== 'installer';
    $('setup-progress').hidden = !busy;
    $('setup-pause').hidden = source() !== 'download';
    for (const id of ['setup-edition', 'setup-model', 'setup-name', 'setup-cd']) $(id).disabled = busy || loading;
    if (active) $('notice').textContent = 'Set up your first classic Mac in a few simple steps.';
  }
  function go(next) {
    step = next;
    $('setup-error').textContent = '';
    render();
    $('setup-title').focus();
  }
  function showError(error) { $('setup-error').textContent = error.message || String(error); }
  function chooseEdition() {
    const row = entry();
    $('setup-name').value = row?.name ?? '';
    $('setup-summary').textContent = row?.summary ?? 'The Library is unavailable. Try again, or go back to use your own installer.';
    $('setup-size').textContent = row ? `${bytes(row.archiveBytes)} download · ${bytes(row.diskCapacityBytes || row.installedBytes)} virtual disk` : '';
    render();
  }
  async function loadCatalog() {
    loading = true; $('setup-summary').textContent = 'Finding pre-installed Macs…'; $('setup-error').textContent = ''; render();
    try {
      const rows = (await api.catalog()).filter(row => row.available);
      // Keep the newest version of each edition, just as the Mac Library does.
      const newest = new Map();
      for (const row of rows) {
        const match = row.id.match(/^(.*)-v(\d+)$/), family = match?.[1] ?? row.id, version = Number(match?.[2] ?? 0);
        if (!newest.has(family) || newest.get(family).version < version) newest.set(family, { row, version });
      }
      entries = [...newest.values()].map(value => value.row);
      $('setup-edition').replaceChildren(...entries.map(row => new Option(row.name, row.id)));
      chooseEdition();
    } catch (error) { entries = []; showError(error); }
    finally { loading = false; $('setup-retry').hidden = entries.length > 0; render(); }
  }
  function ready(id, name) {
    readyID = id;
    $('setup-ready-name').textContent = name;
    $('setup-ready-text').textContent = source() === 'installer' ? 'Start the installer, then follow these steps inside Mac OS:' : 'Everything is in place. Start your Mac to go straight to the classic desktop.';
    $('setup-install-help').hidden = source() !== 'installer';
    $('setup-install-help').textContent = $('setup-model').value === 'powerMacG4'
      ? '1. Open Utilities → Drive Setup on the CD and initialize the blank hard disk.\n2. Open Installer. In Options…, turn off Update Apple Hard Disk Drivers, then install Mac OS.\n3. Restart when installation finishes. Your Mac will start from its hard disk.'
      : '1. Use the disk utility on your CD to initialize the blank hard disk.\n2. Run the Mac OS installer and follow its instructions.\n3. Shut down when finished, eject the CD from Media, then start your Mac.';
    go(3);
  }
  $('setup-license').onchange = render;
  $('setup-name').oninput = render;
  $('setup-edition').onchange = () => { paused = false; chooseEdition(); };
  for (const radio of document.querySelectorAll('input[name="setup-source"]')) radio.onchange = render;
  $('setup-model').onchange = () => { $('setup-name').value = $('setup-model').value === 'quadra800' ? 'Mac OS 8.1' : 'Mac OS 9.2'; render(); };
  $('setup-back').onclick = () => { if (!busy && !loading) go(step - 1); };
  $('setup-retry').onclick = loadCatalog;
  $('setup-cd').onclick = async () => {
    try { const name = await api.chooseInstallCD(); if (name) { cdChosen = true; $('setup-cd-name').textContent = name; } render(); } catch (error) { showError(error); }
  };
  $('setup-open').onclick = async () => {
    try {
      const id = await api.open();
      if (id) { document.querySelector('input[name="setup-source"][value="download"]').checked = true; ready(id, (await api.state()).find(row => row.id === id)?.name ?? 'Your Mac'); }
    } catch (error) { showError(error); }
  };
  $('setup-pause').onclick = () => { $('setup-pause').disabled = true; api.pauseDownload(); };
  api.onDownloadProgress(progress => {
    if (!active || !busy) return;
    const fraction = progress.total > 0 ? Math.min(1, progress.completed / progress.total) : 0;
    $('setup-activity').textContent = `${progress.phase}… ${Math.floor(fraction * 100)}%`;
    $('setup-bar').value = fraction;
  });
  async function complete(start) {
    busy = true; render();
    try {
      await finish(readyID, start);
      active = false;
      $('notice').textContent = start ? 'Use Shut Down from Mac OS before closing ClassicMac.' : 'Choose Start Up, or use Media to select an installation CD.';
    } catch (error) { showError(error); }
    finally { busy = false; render(); }
  }
  $('setup-later').onclick = () => complete(false);
  $('setup-next').onclick = async () => {
    if ($('setup-next').disabled) return;
    if (step === 0) { if ($('setup-license').checked) go(1); return; }
    if (step === 1) {
      go(2);
      if (source() === 'download') { if (!entries.length) await loadCatalog(); else chooseEdition(); }
      else { $('setup-name').value = $('setup-model').value === 'quadra800' ? 'Mac OS 8.1' : 'Mac OS 9.2'; render(); }
      return;
    }
    if (step === 3) { await complete(true); return; }
    busy = true; $('setup-error').textContent = ''; $('setup-pause').disabled = false;
    $('setup-activity').textContent = source() === 'download' ? 'Starting your download…' : 'Creating your Mac and copying the CD image…';
    $('setup-bar').removeAttribute('value'); render();
    try {
      const name = $('setup-name').value.trim();
      const id = source() === 'download' ? await api.download(entry().id, name, true) : await api.setupCreate({ name, machineFamily: $('setup-model').value });
      if (id) ready(id, name);
      else { paused = true; $('setup-error').textContent = 'Paused. Your download is saved. Choose Resume Download to continue.'; }
    } catch (error) {
      paused = source() === 'download';
      if (String(error.message).includes('abort')) $('setup-error').textContent = 'Paused. Your download is saved. Choose Resume Download to continue.';
      else showError(error);
    } finally { busy = false; render(); }
  };
  return {
    sync(rows) {
      if (!rows.length && !active) { active = true; go(0); }
      render();
    },
    evidence() { return { active, step: steps[step], busy, acknowledged: $('setup-license').checked }; }
  };
}
