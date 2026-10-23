'use strict';
const { contextBridge, ipcRenderer } = require('electron');
const api = {};
for (const name of ['state', 'open', 'create', 'setupCreate', 'chooseInstallCD', 'save', 'floppy', 'disc', 'start', 'pause', 'resume', 'shutdown', 'restart', 'forceOff', 'reveal', 'catalog', 'download', 'pauseDownload', 'paste', 'cancelPaste', 'chooseShare', 'platform', 'windowAction', 'resizeWindow']) {
  api[name] = (...args) => ipcRenderer.invoke(name, ...args);
}
api.onState = callback => { ipcRenderer.on('state', (_event, state) => callback(state)); };
api.onProblem = callback => { ipcRenderer.on('problem', (_event, message) => callback(message)); };
api.onWindowState = callback => { ipcRenderer.on('window-state', (_event, state) => callback(state)); };
for (const [method,event] of [['onDownloadProgress','download-progress'],['onPasteProgress','paste-progress']]) api[method]=callback=>ipcRenderer.on(event,(_event,value)=>callback(value));
contextBridge.exposeInMainWorld('classicmac', api);
