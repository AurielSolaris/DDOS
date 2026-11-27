import React, { useEffect, useRef, useState } from 'react'
import { Activity, Calculator, FileText, FolderOpen, Search, Settings, Terminal, X } from 'lucide-react'
import { Wallpaper } from './wallpaper'

export function Mark({ className }) {
    return <span className={['mark', className].filter(Boolean).join(' ')} aria-hidden="true"><i/><i/><i/><i/></span>
}

const apps = [
    { id: 'calculator', title: 'Calculator', icon: Calculator },
    { id: 'settings', title: 'Settings', icon: Settings },
    { id: 'files', title: 'Files', icon: FolderOpen },
    { id: 'notes', title: 'Notepad', icon: FileText },
    { id: 'tasks', title: 'Task Manager', icon: Activity },
    { id: 'terminal', title: 'Terminal', icon: Terminal },
]

const specs = [
    ['Operating system', 'DDOS'], ['Kernel', 'EDOS'], ['Architecture', '32-bit x86'],
    ['Firmware', 'BIOS / x64 UEFI'], ['Desktop', 'Nano-X / NanoWM'], ['Filesystem', 'ext2'],
]

const keys = ['C', '÷', '×', '−', '7', '8', '9', '+', '4', '5', '6', '=', '1', '2', '3', '.', '0']
const operators = {
    '+': (a, b) => a + b,
    '−': (a, b) => a - b,
    '×': (a, b) => a * b,
    '÷': (a, b) => a / b,
}
const initial = { value: '0', left: null, operator: null, awaiting: false }

function CalculatorDemo() {
    const [state, setState] = useState(initial)

    function press(key) {
        setState((previous) => {
            if (key === 'C') return initial
            if (key === '=' || operators[key]) {
                const calculate = previous.operator && !previous.awaiting
                const answer = calculate ? operators[previous.operator](previous.left, Number(previous.value)) : Number(previous.value)
                if (!Number.isFinite(answer)) return { ...initial, value: 'Error', awaiting: true }
                const value = String(Number(answer.toPrecision(10)))
                return { value, left: key === '=' ? null : answer, operator: key === '=' ? null : key, awaiting: true }
            }
            const active = !previous.awaiting && previous.value !== 'Error' ? previous.value : '0'
            if ((key === '.' && active.includes('.')) || active.length >= 13) return previous
            const value = active === '0' && key !== '.' ? key : active + key
            return { ...previous, value, awaiting: false }
        })
    }

    return <div className="calculator-demo">
        <output aria-live="polite">{state.value}</output>
        <div>{keys.map((key) => <button key={key} type="button" onClick={() => press(key)} data-key={key}>{key}</button>)}</div>
    </div>
}

function AppContent({ active, note, updateNote }) {
    switch (active) {
        case 'settings':
            return <><div className="window-heading"><Settings size={17}/> System information</div>
                <dl className="spec-table">{specs.map(([key, value]) => <div key={key}><dt>{key}</dt><dd>{value}</dd></div>)}</dl>
                <div className="window-status"><span/> Graphical desktop</div></>
        case 'calculator':
            return <CalculatorDemo />
        case 'notes':
            return <textarea aria-label="Notepad" value={note} onChange={(event) => updateNote(event.target.value)} spellCheck={false} />
        case 'files':
            return <dl className="spec-table drive-list">
                <div><dt>A:/</dt><dd>Boot volume</dd></div><div><dt>B:/</dt><dd>System volume</dd></div>
                <div><dt>C:/</dt><dd>Reserved swap</dd></div><div><dt>D:/</dt><dd>User data</dd></div>
            </dl>
        case 'tasks':
            return <><div className="window-heading"><Activity size={17}/> Desktop preview</div>
                <p className="window-copy">Native DDOS Task Manager reports mapped memory for each process.</p></>
        case 'terminal':
            return <div className="terminal-demo"><p>D:/&gt; VER</p><p>DDOS — DOS Derived OS</p><p>32-bit protected mode</p><p>D:/&gt; <span>▌</span></p></div>
        default:
            return null
    }
}

export function Desktop({ expanded = false }) {
    const [active, setActive] = useState('settings')
    const [menu, setMenu] = useState(false)
    const [query, setQuery] = useState('')
    const [note, setNote] = useState('Welcome to DDOS.\n\nThis is a browser preview of Notepad.')
    const [time, setTime] = useState(() => new Date())
    const searchInput = useRef(null)
    const trigger = useRef(null)
    const visible = apps.filter((app) => app.title.toLowerCase().includes(query.trim().toLowerCase()))
    const app = apps.find(({ id }) => id === active)
    const AppIcon = app?.icon

    useEffect(() => {
        const timer = setInterval(() => setTime(new Date()), 30_000)
        return () => clearInterval(timer)
    }, [])

    useEffect(() => {
        if (menu) searchInput.current?.focus()
    }, [menu])

    function showApp(id) {
        setActive(id)
        setMenu(false)
        setQuery('')
    }

    function closeMenu() {
        setMenu(false)
        setQuery('')
        trigger.current?.focus()
    }

    function desktopKeys(event) {
        if (event.key === 'Escape' && menu) {
            event.preventDefault()
            closeMenu()
        }
    }

    return <div className={'desktop-frame' + (expanded ? ' desktop-frame--expanded' : '')} onKeyDown={desktopKeys}>
        <div className="frame-bar"><span className="frame-lights" aria-hidden="true"><i/><i/><i/></span>
            <span>DDOS Desktop</span><span className="live-label"><i/> INTERACTIVE</span></div>
        <div className="desktop-screen">
            <Wallpaper />
            <div className="desktop-shortcuts" aria-label="Desktop shortcuts">
                {apps.slice(0, 4).map(({ id, title, icon: Icon }) => <button key={id} type="button" onClick={() => showApp(id)}>
                    <span><Icon size={18} strokeWidth={1.7}/></span>{title}
                </button>)}
            </div>
            {app && <section className="demo-window" aria-label={app.title}>
                <header><AppIcon size={14}/><strong>{app.title}</strong>
                    <button type="button" aria-label={'Close ' + app.title} onClick={() => setActive(null)}><X size={15}/></button>
                </header>
                <div className="demo-window-content"><AppContent key={active} active={active} note={note} updateNote={setNote}/></div>
            </section>}
            {menu && <div className="demo-launcher">
                <form role="search" onSubmit={(event) => { event.preventDefault(); if (visible.length) showApp(visible[0].id) }}>
                    <Search size={16}/><input aria-label="Search apps" ref={searchInput} placeholder="Search apps" value={query} onChange={(event) => setQuery(event.target.value)}/>
                </form>
                {visible.map(({ id, title, icon: Icon }) =>
                    <button key={id} type="button" onClick={() => showApp(id)}><Icon size={16}/>{title}</button>
                )}
                {!visible.length && <p>No matching apps</p>}
            </div>}
            <div className="desktop-taskbar">
                <button type="button" ref={trigger} aria-expanded={menu} onClick={() => menu ? closeMenu() : setMenu(true)}><Mark/> DDOS</button>
                <span className="taskbar-open">{app && <><AppIcon size={14}/>{app.title}</>}</span>
                <time dateTime={time.toISOString()}>{time.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', hour12: false, timeZone: 'UTC' })} UTC</time>
            </div>
        </div>
    </div>
}
