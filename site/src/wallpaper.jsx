import React, { useEffect, useRef } from 'react'

const vertex = `
attribute vec2 position;
void main() { gl_Position = vec4(position, 0.0, 1.0); }
`
const fragment = `
precision mediump float;
uniform vec2 resolution;
uniform vec2 pointer;

float ribbon(float y, float center, float width) {
    return exp(-abs(y - center) / width);
}

void main() {
    vec2 uv = gl_FragCoord.xy / resolution;
    float x = uv.x;
    float y = uv.y;
    float bend = pow(x, 1.5);
    float shift = (pointer.y - 0.5) * 0.025;
    float ridge = 0.19 + 0.52 * bend + shift;
    float light = ribbon(y, ridge, 0.075);
    float mist = ribbon(y, ridge, 0.19);
    float upper = ribbon(y, ridge + 0.07, 0.010);
    float lower = ribbon(y, ridge - 0.13 + x * 0.045, 0.033);
    float glow = exp(-length((uv - vec2(0.75, 0.57)) * vec2(1.5, 1.65)) * 3.8);
    vec3 color = vec3(0.022, 0.057, 0.15);
    color += vec3(0.02, 0.20, 0.49) * mist * (0.43 + 0.57 * x);
    color += vec3(0.105, 0.24, 0.50) * light * (0.30 + 0.40 * bend);
    color += vec3(0.02, 0.30, 0.69) * upper * (0.22 + 0.35 * x);
    color += vec3(0.11, 0.05, 0.31) * lower;
    color += vec3(0.05, 0.16, 0.36) * glow;
    float grain = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    gl_FragColor = vec4(color + 0.015 * grain, 1.0);
}
`

function compile(gl, type, source) {
    const shader = gl.createShader(type)
    gl.shaderSource(shader, source)
    gl.compileShader(shader)
    if (gl.getShaderParameter(shader, gl.COMPILE_STATUS)) return shader
    gl.deleteShader(shader)
}

export function Wallpaper() {
    const canvas = useRef(null)

    useEffect(() => {
        const surface = canvas.current
        const gl = surface.getContext('webgl', { antialias: false, alpha: false })
        if (!gl) return

        const vs = compile(gl, gl.VERTEX_SHADER, vertex)
        const fs = compile(gl, gl.FRAGMENT_SHADER, fragment)
        const program = gl.createProgram()
        if (vs) gl.attachShader(program, vs)
        if (fs) gl.attachShader(program, fs)
        gl.linkProgram(program)
        if (vs) gl.deleteShader(vs)
        if (fs) gl.deleteShader(fs)
        if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
            gl.deleteProgram(program)
            return
        }

        gl.useProgram(program)
        const buffer = gl.createBuffer()
        gl.bindBuffer(gl.ARRAY_BUFFER, buffer)
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW)
        const position = gl.getAttribLocation(program, 'position')
        gl.enableVertexAttribArray(position)
        gl.vertexAttribPointer(position, 2, gl.FLOAT, false, 0, 0)
        const resolution = gl.getUniformLocation(program, 'resolution')
        const pointer = gl.getUniformLocation(program, 'pointer')
        const host = surface.parentElement
        const reduced = matchMedia('(prefers-reduced-motion: reduce)')
        let frame = 0
        let mouseY = 0.5

        function draw() {
            frame = 0
            const scale = Math.min(devicePixelRatio || 1, 1.5)
            const width = Math.max(1, Math.round(surface.clientWidth * scale))
            const height = Math.max(1, Math.round(surface.clientHeight * scale))
            if (surface.width !== width || surface.height !== height) {
                surface.width = width
                surface.height = height
            }
            gl.viewport(0, 0, width, height)
            gl.uniform2f(resolution, width, height)
            gl.uniform2f(pointer, 0.5, reduced.matches ? 0.5 : mouseY)
            gl.drawArrays(gl.TRIANGLES, 0, 3)
            surface.classList.add('wallpaper--active')
        }

        function repaint() {
            if (!frame) frame = requestAnimationFrame(draw)
        }

        function move(event) {
            if (reduced.matches || event.pointerType !== 'mouse') return
            const bounds = host.getBoundingClientRect()
            mouseY = 1 - (event.clientY - bounds.top) / bounds.height
            repaint()
        }

        function contextLost(event) {
            event.preventDefault()
            surface.classList.remove('wallpaper--active')
        }

        const observer = new ResizeObserver(repaint)
        observer.observe(surface)
        host.addEventListener('pointermove', move)
        surface.addEventListener('webglcontextlost', contextLost)
        repaint()

        return () => {
            observer.disconnect()
            host.removeEventListener('pointermove', move)
            surface.removeEventListener('webglcontextlost', contextLost)
            cancelAnimationFrame(frame)
            gl.deleteBuffer(buffer)
            gl.deleteProgram(program)
        }
    }, [])

    return <canvas ref={canvas} className="wallpaper" aria-hidden="true" />
}
