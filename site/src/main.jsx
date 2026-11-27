import React, { useState } from 'react'
import { createRoot } from 'react-dom/client'
import { createHashHistory, createRootRoute, createRoute, createRouter, Link, Outlet, RouterProvider } from '@tanstack/react-router'
import { Check, CircuitBoard, Code2, Copy, Cpu, FolderTree, Github, HardDrive, Layers3, Monitor, Play, Terminal } from 'lucide-react'
import { Desktop, Mark } from './desktop'
import './style.scss'

const source = 'https://github.com/AurielSolaris/DDOS'
const guide = source + '/blob/main/README.md#build-and-run'

function SourceLink({ className = 'button button--quiet', children = 'Source code' }) {
    return <a className={className} href={source}><Github size={17}/>{children}</a>
}

function Layout() {
    return <div className="site">
        <header className="site-header shell">
            <Link to="/" className="brand" aria-label="DDOS home"><Mark/><strong>DDOS</strong></Link>
            <nav aria-label="Main navigation">
                <Link to="/" activeOptions={{ exact: true }}>Overview</Link>
                <Link to="/desktop">Desktop</Link>
                <Link to="/get-started">Get started</Link>
            </nav>
            <SourceLink className="header-source">GitHub</SourceLink>
        </header>
        <main><Outlet/></main>
        <footer className="site-footer shell">
            <Link to="/" className="brand"><Mark/><strong>DDOS</strong></Link>
            <p>DOS Derived OS · Open source under MIT</p>
            <SourceLink className="header-source">GitHub</SourceLink>
        </footer>
    </div>
}

const facts = [
    [Cpu, '32-bit x86', 'EDOS kernel'],
    [CircuitBoard, 'BIOS + UEFI', 'Two boot paths'],
    [HardDrive, 'ext2', 'Persistent user volume'],
    [Layers3, 'Nano-X', 'Graphical desktop'],
]

const features = [
    [Monitor, 'A desktop with room to work.', 'Floating NanoWM windows, a searchable launcher, and frosted panels sit above your choice of wallpaper. The taskbar keeps the clock and app menu in reach.'],
    [FolderTree, 'Familiar drive letters.', 'Browse system files on B:/ and keep personal files on D:/. Copy, rename, or delete files with File Manager, and make quick edits in Notepad.'],
    [Code2, 'Built close to the hardware.', 'The EDOS kernel is written in C and NASM. The project supports a 32-bit native ELF environment and runs DOS COM and MZ programs through its BIOS path.'],
]

function FeatureCards() {
    return <section className="features shell" aria-labelledby="features-heading">
        <div className="section-heading">
            <span className="kicker">EXPLORE DDOS</span>
            <h2 id="features-heading">From prompt to desktop.</h2>
        </div>
        <div className="feature-grid">
            {features.map(([Icon, title, copy]) => <article className="glass-card" key={title}>
                <span className="feature-icon"><Icon size={23} strokeWidth={1.6}/></span>
                <h3>{title}</h3><p>{copy}</p>
            </article>)}
        </div>
        <div className="boot-cta">
            <div><Terminal size={24}/><div><h2>Boot DDOS in QEMU.</h2><p>Build from source and open the graphical desktop.</p></div></div>
            <Link className="button button--primary" to="/get-started">Get started</Link>
        </div>
    </section>
}

function Overview() {
    return <>
        <section className="hero shell" aria-labelledby="hero-heading">
            <div className="hero-content">
                <div className="kicker hero-kicker"><span/> DOS DERIVED OPERATING SYSTEM</div>
                <h1 id="hero-heading">A new life<br/>for <em>DOS.</em></h1>
                <p>A 32-bit operating system with DOS roots, a floating desktop, and a kernel written in C and NASM.</p>
                <div className="hero-actions">
                    <Link className="button button--primary" to="/desktop"><Play size={16} fill="currentColor"/> Try the desktop</Link>
                    <SourceLink/>
                </div>
                <p className="hero-footnote">Open source <span/> Built for x86</p>
            </div>
            <Desktop/>
        </section>
        <dl className="fact-strip shell">
            {facts.map(([Icon, title, caption]) => <div key={title}>
                <Icon size={22} strokeWidth={1.6}/><div><dt>{title}</dt><dd>{caption}</dd></div>
            </div>)}
        </dl>
        <FeatureCards/>
    </>
}

function DesktopPage() {
    return <section className="subpage shell">
        <div className="section-heading">
            <span className="kicker">INTERACTIVE BROWSER PREVIEW</span>
            <h1>Explore the desktop.</h1>
            <p>Open an app from the desktop or use the DDOS button to search. This preview runs in your browser.</p>
        </div>
        <Desktop expanded/>
        <p className="desktop-next"><Link to="/get-started">Build and boot DDOS in QEMU</Link></p>
    </section>
}

const steps = [
    ['Get the source', 'Install Python, NASM, LLVM, and QEMU. For graphical UEFI boot, install EDK II or OVMF firmware as well.', 'git clone https://github.com/AurielSolaris/DDOS.git\ncd DDOS'],
    ['Build the desktop', 'Set up the musl, BusyBox, runit, and Microwindows dependencies using the repository build guide first.', 'sh tools/build-microwindows.sh\npython build.py --desktop'],
    ['Boot and launch', 'Run the boot command on your host. At the DDOS guest prompt, type NANOWM and press Enter.', 'python boot.py --uefi'],
]

function CommandBlock({ command }) {
    const [status, setStatus] = useState('')
    async function copy() {
        try {
            await navigator.clipboard.writeText(command)
            setStatus('Copied')
        } catch {
            setStatus('Select the command to copy it')
        }
    }

    return <div className="command">
        <pre><code>{command}</code></pre>
        <button type="button" className="button button--quiet" onClick={copy}>{status === 'Copied' ? <Check size={15}/> : <Copy size={15}/>} {status === 'Copied' ? 'Copied' : 'Copy'}</button>
        <span role="status" className="copy-status">{status}</span>
    </div>
}

function GetStarted() {
    return <section className="subpage shell">
        <div className="section-heading">
            <span className="kicker">BUILD FROM SOURCE</span>
            <h1>Start DDOS in QEMU.</h1>
            <p>Use the project's build scripts to create the boot and system images.</p>
        </div>
        <ol className="build-steps">
            {steps.map(([title, copy, command]) => <li className="glass-card" key={title}>
                <h2>{title}</h2><p>{copy}</p><CommandBlock command={command}/>
            </li>)}
        </ol>
        <a className="button button--quiet" href={guide}><Terminal size={17}/> Read the full build guide</a>
    </section>
}

function NotFound() {
    return <section className="subpage shell"><h1>Page not found.</h1><Link to="/" className="button button--primary">Back to overview</Link></section>
}

const rootRoute = createRootRoute({ component: Layout, notFoundComponent: NotFound })
const overviewRoute = createRoute({ getParentRoute: () => rootRoute, path: '/', component: Overview })
const desktopRoute = createRoute({ getParentRoute: () => rootRoute, path: 'desktop', component: DesktopPage })
const startedRoute = createRoute({ getParentRoute: () => rootRoute, path: 'get-started', component: GetStarted })
const router = createRouter({
    routeTree: rootRoute.addChildren([overviewRoute, desktopRoute, startedRoute]),
    history: createHashHistory(),
    scrollRestoration: true,
})

createRoot(document.getElementById('root')).render(<RouterProvider router={router}/>)
