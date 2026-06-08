import * as p from '@clack/prompts';
import { detectAgents, installAgentConfig } from './engine';

export async function runInstaller() {
  p.intro('Codegraph-X Installer');

  const agents = detectAgents();
  if (agents.length === 0) {
    p.log.warn('No supported AI agents found on your system.');
    p.outro('Aborting.');
    return;
  }

  p.log.info(`Found ${agents.length} supported agent(s) on your system.`);

  for (const agent of agents) {
    if (agent.alreadyConfigured) {
      p.log.success(`[${agent.name}] is already configured!`);
      continue;
    }

    const installConfirm = await p.confirm({
      message: `Do you want to configure Codegraph-X for ${agent.name}?`
    });

    if (p.isCancel(installConfirm)) {
      p.cancel('Installation cancelled.');
      process.exit(0);
    }

    if (installConfirm) {
      // Determine scope
      let scope: 'global' | 'local' = 'global';
      if (agent.isInstalledLocal && agent.isInstalledGlobal) {
        const scopeChoice = await p.select({
          message: `Where do you want to configure ${agent.name}?`,
          options: [
            { value: 'global', label: 'Global (User profile)' },
            { value: 'local', label: 'Local (Current project)' }
          ]
        });
        if (p.isCancel(scopeChoice)) {
          p.cancel('Installation cancelled.');
          process.exit(0);
        }
        scope = scopeChoice as 'global' | 'local';
      } else if (agent.isInstalledLocal) {
        scope = 'local';
      }

      const success = installAgentConfig(agent.id, scope);
      if (success) {
        p.log.success(`Successfully configured ${agent.name} (${scope}).`);
      } else {
        p.log.error(`Failed to configure ${agent.name}.`);
      }
    }
  }

  p.outro('Setup complete!');
}
