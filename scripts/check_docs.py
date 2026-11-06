#!/usr/bin/env python3
"""Проверка согласованности документации с деревом репозитория.

Ловит три класса расхождений, которые появляются сами собой по ходу работы:
  * в документации упомянут файл/скрипт, которого нет;
  * в scripts/ или golden/ лежит файл, о котором нигде не сказано;
  * остались заглушки вида «появится на M…», TODO, «не реализовано».

    check_docs.py          проверить
    --quiet                без списка проверок, только итог
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = ['README.md', 'docs/roadmap.md', 'docs/porting-notes.md', 'docs/ANALYSIS-4.md']
TRACKED_DIRS = ['scripts', 'golden']

STALE_PATTERNS = [
    (r'появится на M', 'заглушка «появится на M…»'),
    (r'\bTODO\b', 'TODO'),
    (r'\bFIXME\b', 'FIXME'),
    (r'не реализовано', '«не реализовано»'),
    (r'not implemented yet', '«not implemented yet»'),
]

PATH_RE = re.compile(r'\b((?:scripts|golden|docs|ref-build|port)/[A-Za-z0-9_./*-]+)')


def main():
    quiet = '--quiet' in sys.argv
    problems = []

    texts = {}
    for doc in DOCS:
        path = os.path.join(ROOT, doc)
        if not os.path.isfile(path):
            problems.append('%s: файла нет' % doc)
            continue
        texts[doc] = open(path, encoding='utf-8').read()

    # 1. упомянутые пути существуют
    mentioned = set()
    for doc, text in texts.items():
        for match in PATH_RE.finditer(text):
            reference = match.group(1).rstrip('.,;:)')
            if '*' in reference:                     # шаблоны (например golden/expected/*.ltc)
                continue
            mentioned.add(reference)
            if not os.path.exists(os.path.join(ROOT, reference)):
                problems.append('%s: упомянут несуществующий путь %s' % (doc, reference))

    # 2. каждый файл из scripts/ и golden/ упомянут хотя бы где-то
    for directory in TRACKED_DIRS:
        base = os.path.join(ROOT, directory)
        if not os.path.isdir(base):
            continue
        for name in sorted(os.listdir(base)):
            path = os.path.join(base, name)
            if os.path.isdir(path):
                if name in ('expected', 'fixtures'):
                    continue
                continue
            reference = '%s/%s' % (directory, name)
            # годится либо полный путь, либо имя файла под заголовком каталога в README
            if not any(reference in text or name in text for text in texts.values()):
                problems.append('%s: файл не упомянут в документации' % reference)

    # 3. заглушки
    for doc, text in texts.items():
        for pattern, description in STALE_PATTERNS:
            for match in re.finditer(pattern, text):
                line = text.count('\n', 0, match.start()) + 1
                problems.append('%s:%d: осталась заглушка (%s)' % (doc, line, description))

    # 4. статусная таблица README: все этапы «готово»
    readme = texts.get('README.md', '')
    for milestone in ('M0', 'M1', 'M2', 'M3', 'M4'):
        row = re.search(r'\|\s*%s\s*\|[^|]*\|([^|]*)\|' % milestone, readme)
        if not row:
            problems.append('README.md: в таблице статуса нет строки %s' % milestone)
        elif 'готово' not in row.group(1):
            problems.append('README.md: этап %s не помечен как «готово»' % milestone)

    if not quiet:
        print('документов проверено: %d, упомянутых путей: %d' % (len(texts), len(mentioned)))
    if problems:
        for problem in problems:
            print('  ! %s' % problem)
        print('check_docs: %d проблем' % len(problems))
        return 1
    print('check_docs: OK — документация согласована с деревом')
    return 0


if __name__ == '__main__':
    sys.exit(main())
