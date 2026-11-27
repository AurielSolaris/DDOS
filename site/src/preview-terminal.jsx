import React, { useEffect, useRef, useState } from 'react'

const welcome = 'DDOS browser terminal. Type HELP for commands.'
const drives = {
    'A:/': ['Boot volume'],
    'B:/': ['bin/', 'etc/', 'fonts/', 'tmp/'],
    'D:/': [],
    'B:/bin': ['calculator', 'file_manager', 'nano-X', 'nanowm', 'notepad', 'settings', 'snake', 'task_mgr'],
    'B:/etc': [],
    'B:/fonts': ['Inter.woff'],
    'B:/tmp': [],
}

function resolvePath(cwd, path) {
    const drive = /^([a-z]):/i.exec(path)
    const root = drive ? drive[1].toUpperCase() + ':' : cwd.slice(0, 2)
    const relative = drive ? path.slice(2) : path
    const parts = drive || relative.startsWith('/') ? [] : cwd.slice(3).split('/').filter(Boolean)
    for (const part of relative.split('/')) {
        if (part === '..') parts.pop()
        else if (part && part !== '.') parts.push(part.toLowerCase())
    }
    return root + '/' + parts.join('/')
}

function execute(cwd, line) {
    const [, command, arg = ''] = /^(\S+)(?:\s+(.*))?$/.exec(line.trim())
    const path = resolvePath(cwd, arg)
    switch (command.toUpperCase()) {
        case 'HELP': return { output: 'HELP  VER  CLS  ECHO [text]\nPWD  CD [path]  DIR [path]  LS [path]\nUse A:, B:, or D: to switch drives.' }
        case 'VER': return { output: 'DDOS - DOS Derived OS\nEDOS kernel, 32-bit x86 (browser preview)' }
        case 'ECHO': return { output: arg }
        case 'CLS': return { clear: true }
        case 'PWD': return { output: cwd }
        case 'CD':
            if (!arg) return { output: cwd }
            return Object.hasOwn(drives, path) ? { cwd: path } : { output: 'Path not found: ' + path }
        case 'DIR':
        case 'LS':
            return { output: Object.hasOwn(drives, path) ? drives[path].join('\n') || '(empty)' : 'Path not found: ' + path }
        default:
            if (/^[a-z]:$/i.test(command) && !arg) {
                const root = command.toUpperCase() + '/'
                return Object.hasOwn(drives, root) ? { cwd: root } : { output: 'Drive unavailable: ' + command.toUpperCase() }
            }
            return { output: 'Bad command or file name: ' + command }
    }
}

export function TerminalDemo({ foreground }) {
    const [cwd, setCwd] = useState('D:/')
    const [line, setLine] = useState('')
    const [transcript, setTranscript] = useState([welcome])
    const input = useRef(null)
    const screen = useRef(null)
    const history = useRef([])
    const historyIndex = useRef(null)
    const draft = useRef('')

    useEffect(() => {
        if (foreground) input.current?.focus({ preventScroll: true })
    }, [foreground])

    useEffect(() => {
        const viewport = screen.current
        viewport.scrollTop = viewport.scrollHeight
    }, [transcript])

    function submit(event) {
        event.preventDefault()
        const prompt = cwd + '> ' + line
        const command = line.trim()
        setLine('')
        historyIndex.current = null
        if (!command) {
            setTranscript((previous) => [...previous, prompt])
            return
        }
        history.current.push(line)
        const result = execute(cwd, line)
        if (result.cwd) setCwd(result.cwd)
        setTranscript((previous) => result.clear ? [] : [...previous, prompt, result.output].filter(Boolean))
    }

    function handleKey(event) {
        if (event.key === 'ArrowUp' || event.key === 'ArrowDown') {
            if (!history.current.length) return
            event.preventDefault()
            const end = history.current.length
            const current = historyIndex.current ?? end
            if (historyIndex.current === null) draft.current = line
            const next = Math.min(end, Math.max(0, current + (event.key === 'ArrowUp' ? -1 : 1)))
            historyIndex.current = next === end ? null : next
            setLine(next === end ? draft.current : history.current[next])
        } else if (event.ctrlKey && event.key.toLowerCase() === 'l') {
            event.preventDefault()
            setTranscript([])
        } else if (event.ctrlKey && event.key.toLowerCase() === 'c' &&
                   input.current.selectionStart === input.current.selectionEnd && !window.getSelection()?.toString()) {
            event.preventDefault()
            setTranscript((previous) => [...previous, cwd + '> ' + line + '^C'])
            historyIndex.current = null
            setLine('')
        }
    }

    return <div className="terminal-demo" ref={screen}>
        <div role="log" aria-label="Terminal output">
            {transcript.map((entry, index) => <p key={index}>{entry}</p>)}
        </div>
        <form onSubmit={submit}>
            <label htmlFor="terminal-command">{cwd}&gt;</label>
            <input id="terminal-command" aria-label="Terminal command" ref={input} value={line}
                onChange={(event) => setLine(event.target.value)} onKeyDown={handleKey}
                autoComplete="off" spellCheck={false} autoCapitalize="off" enterKeyHint="go"/>
        </form>
    </div>
}
