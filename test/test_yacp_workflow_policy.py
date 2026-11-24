"""Keep the YACP build workflow aligned with the single-source policy."""
import ast
import json
from pathlib import Path
import re
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[1]


def workflow(name):
    # BaseLoader preserves the YAML 1.2 `on` key used by GitHub Actions.
    return yaml.load((ROOT / '.github/workflows' / name).read_text(encoding='utf-8'),
                     Loader=yaml.BaseLoader)


class WorkflowPolicyTests(unittest.TestCase):
    def test_standard_profiles_in_one_sequential_wrapper(self):
        module = ast.parse((ROOT / 'scripts/build_yacp.py').read_text(encoding='utf-8'))
        profiles = next(ast.literal_eval(node.value) for node in module.body
                        if isinstance(node, ast.Assign)
                        and any(isinstance(target, ast.Name) and target.id == 'ENVIRONMENTS'
                                for target in node.targets))
        self.assertEqual(set(profiles), {'tiny', 'x4c', 'x4pro'})
        policy = json.loads((ROOT / '.yacp-source.json').read_text(encoding='utf-8'))
        self.assertEqual(set(profiles), set(policy['hardware']))
        for config in ('platformio.ini', 'platformio.simulator.ini'):
            sections = (ROOT / config).read_text(encoding='utf-8')
            self.assertNotRegex(sections, r'(?im)^\[env:[^\]]*xlarge[^\]]*\]')
        build = workflow('ci.yml')['jobs']['build']
        commands = '\n'.join(step.get('run', '') for step in build['steps'])
        self.assertIn('python scripts/build_yacp.py all-local', commands)
        self.assertNotRegex(commands, r'\bpio\s+(?:run|check)\b')
        self.assertNotIn('strategy', build)

    def test_fresh_checkout_has_full_history_and_submodules(self):
        jobs = workflow('ci.yml')['jobs']
        for job in ('source-policy', 'build'):
            checkout = next(step for step in jobs[job]['steps']
                            if step.get('uses', '').startswith('actions/checkout@'))
            self.assertEqual(checkout['with']['fetch-depth'], '0')
            self.assertEqual(checkout['with']['submodules'], 'recursive')
            self.assertTrue(any('check_yacp_source.py --register' in step.get('run', '')
                                for step in jobs[job]['steps']))

    def test_required_check_includes_policy_and_compilation(self):
        jobs = workflow('ci.yml')['jobs']
        self.assertEqual(jobs['build']['needs'], 'source-policy')
        self.assertTrue({'source-policy', 'build'} <= set(jobs['test-status']['needs']))
        commands = '\n'.join(step.get('run', '') for step in jobs['source-policy']['steps'])
        self.assertIn('test_yacp*.py', commands)
        self.assertIn('HomeMenuLayoutTest.cpp', commands)
        self.assertLess(commands.index('mkdir -p qa-artifacts'), commands.index('unittest discover'))
        self.assertLess(commands.index('scripts/gen_i18n.py'), commands.index('unittest discover'))
        build_commands = '\n'.join(step.get('run', '') for step in jobs['build']['steps'])
        for package in ('freetype-py', 'fonttools', 'Pillow'):
            self.assertIn(package, build_commands)

    def test_ci_only_retains_named_artifacts(self):
        data = workflow('ci.yml')
        self.assertEqual(data['permissions'], {'contents': 'read'})
        steps = data['jobs']['build']['steps']
        upload = next(step for step in steps
                      if step.get('uses', '').startswith('actions/upload-artifact@'))
        self.assertIn('qa-artifacts/bin/*.bin', upload['with']['path'])
        self.assertIn('qa-artifacts/bin/*.json', upload['with']['path'])
        commands = '\n'.join(step.get('run', '') for step in steps)
        self.assertNotRegex(commands, r'\bgh\s+release|\bgit\s+push')

    def test_inherited_publishers_remain_upstream_only(self):
        expected = "github.repository == 'crosspoint-reader/crosspoint-reader'"
        for name in ('release.yml', 'release_candidate.yml', 'release-fonts.yml',
                     'pr-firmware-links.yml'):
            for job in workflow(name)['jobs'].values():
                self.assertTrue(job.get('if', '').startswith(expected), name)

    def test_embedded_ci_python_is_valid(self):
        commands = [step.get('run', '') for step in workflow('ci.yml')['jobs']['build']['steps']]
        sources = [re.search(r"python - <<'PY'\n(.*?)\nPY\s*$", command, re.S)
                   for command in commands]
        sources = [match[1] for match in sources if match]
        self.assertEqual(len(sources), 1)
        ast.parse(sources[0])


if __name__ == '__main__':
    unittest.main()
