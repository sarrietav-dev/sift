// The queue list in the drawer: tap to open, drag the grip to reorder, swipe
// a row left to reveal Remove. Pointer events cover touch, pen, and mouse.

const REVEAL = 84;

const minutes = words => (words > 0 ? `${Math.max(1, Math.round(words / 220))} min` : "");

export function describe(item) {
    return [item.domain, minutes(item.wordCount)].filter(Boolean).join(" · ");
}

function icon(id) {
    const template = document.getElementById(id);
    return template.content.firstElementChild.cloneNode(true);
}

export class QueueList {
    constructor(list, { onOpen, onRemove, onReorder }) {
        this.list = list;
        this.scroller = list.closest(".drawer-scroll");
        this.onOpen = onOpen;
        this.onRemove = onRemove;
        this.onReorder = onReorder;
        this.items = [];
        this.currentId = null;
        this.pending = [];
        this.openRow = null;
    }

    render(items, currentId, pending = this.pending) {
        this.items = items;
        this.currentId = currentId;
        this.pending = pending;
        this.openRow = null;
        const rows = items.map((item, index) => this.row(item, index));
        for (const label of pending) {
            const li = document.createElement("li");
            li.className = "row pending";
            const main = document.createElement("div");
            main.className = "row-main";
            const spinner = document.createElement("span");
            spinner.className = "spinner";
            const text = document.createElement("div");
            text.className = "row-text";
            const title = document.createElement("span");
            title.className = "row-title";
            title.textContent = "Adding…";
            const meta = document.createElement("span");
            meta.className = "row-meta";
            meta.textContent = label;
            text.append(title, meta);
            main.append(spinner, text);
            li.append(main);
            rows.push(li);
        }
        this.list.replaceChildren(...rows);
    }

    markCurrent(currentId) {
        this.currentId = currentId;
        for (const row of this.list.children) row.classList.toggle("current", row.dataset.id === currentId);
    }

    row(item, index) {
        const li = document.createElement("li");
        li.className = "row";
        li.dataset.id = item.id;
        li.classList.toggle("current", item.id === this.currentId);
        li.classList.toggle("read", item.readState === 2);

        const remove = document.createElement("button");
        remove.className = "row-delete";
        remove.type = "button";
        remove.textContent = "Remove";
        remove.tabIndex = -1;
        remove.addEventListener("click", () => this.remove(li, item));

        const main = document.createElement("div");
        main.className = "row-main";
        main.tabIndex = 0;
        main.setAttribute("role", "button");
        main.setAttribute("aria-label", `${item.title || "Untitled"}, ${index + 1} of ${this.items.length}`);

        const grip = document.createElement("span");
        grip.className = "grip";
        grip.setAttribute("aria-hidden", "true");
        grip.append(icon("icon-grip"));

        const text = document.createElement("div");
        text.className = "row-text";
        const title = document.createElement("span");
        title.className = "row-title";
        title.textContent = item.title || "Untitled";
        const meta = document.createElement("span");
        meta.className = "row-meta";
        meta.textContent = describe(item);
        text.append(title, meta);

        const quick = document.createElement("button");
        quick.className = "row-quick";
        quick.type = "button";
        quick.setAttribute("aria-label", "Remove from queue");
        quick.append(icon("icon-close"));
        quick.addEventListener("click", event => {
            event.stopPropagation();
            this.remove(li, item);
        });

        main.append(grip, text, quick);
        li.append(remove, main);

        main.addEventListener("keydown", event => {
            if (event.key === "Enter" || event.key === " ") {
                event.preventDefault();
                this.onOpen(item);
            } else if (event.key === "Delete" || event.key === "Backspace") {
                event.preventDefault();
                this.remove(li, item);
            } else if (event.altKey && (event.key === "ArrowUp" || event.key === "ArrowDown")) {
                event.preventDefault();
                this.move(index, event.key === "ArrowUp" ? index - 1 : index + 1);
            }
        });
        main.addEventListener("pointerdown", event => this.pointerDown(event, li, main, item));
        return li;
    }

    move(from, to) {
        if (to < 0 || to >= this.items.length || from === to) return;
        const items = [...this.items];
        const [moved] = items.splice(from, 1);
        items.splice(to, 0, moved);
        this.render(items, this.currentId);
        this.list.querySelector(`[data-id="${moved.id}"] .row-main`)?.focus();
        this.onReorder(items.map(item => item.id));
    }

    remove(li, item) {
        li.style.height = li.offsetHeight + "px";
        li.classList.add("removing");
        requestAnimationFrame(() => (li.style.height = "0px"));
        setTimeout(() => this.onRemove(item), 180);
    }

    closeOpenRow(except) {
        if (this.openRow && this.openRow !== except) {
            this.openRow.classList.remove("open");
            this.openRow.querySelector(".row-main").style.transform = "";
            this.openRow = null;
        }
    }

    pointerDown(event, li, main, item) {
        if (event.button !== 0 || event.target.closest(".row-quick")) return;
        const onGrip = !!event.target.closest(".grip");
        const startX = event.clientX;
        const startY = event.clientY;
        const base = li.classList.contains("open") ? -REVEAL : 0;
        let mode = onGrip ? "drag" : null;
        let offset = base;
        let drag = null;
        this.closeOpenRow(li);

        const move = moveEvent => {
            if (moveEvent.pointerId !== event.pointerId) return;
            const dx = moveEvent.clientX - startX;
            const dy = moveEvent.clientY - startY;
            if (!mode) {
                if (Math.abs(dx) > 8 && Math.abs(dx) > Math.abs(dy)) {
                    mode = "swipe";
                    main.setPointerCapture(event.pointerId);
                    li.classList.add("swiping");
                } else if (Math.abs(dy) > 8) {
                    mode = "scroll";
                    return finish();
                }
            }
            if (mode === "swipe") {
                offset = Math.max(-REVEAL - 24, Math.min(0, base + dx));
                main.style.transform = `translateX(${offset}px)`;
            } else if (mode === "drag") {
                drag ??= this.startDrag(li, event);
                this.dragTo(drag, moveEvent.clientY);
            }
        };

        const finish = upEvent => {
            if (upEvent && upEvent.pointerId !== event.pointerId) return;
            window.removeEventListener("pointermove", move);
            window.removeEventListener("pointerup", finish);
            window.removeEventListener("pointercancel", finish);
            li.classList.remove("swiping");
            if (mode === "swipe") {
                const open = offset < -REVEAL / 2;
                li.classList.toggle("open", open);
                main.style.transform = open ? `translateX(${-REVEAL}px)` : "";
                this.openRow = open ? li : null;
            } else if (mode === "drag") {
                if (drag) this.endDrag(drag);
            } else if (!mode && upEvent?.type === "pointerup") {
                if (base) {
                    li.classList.remove("open");
                    main.style.transform = "";
                    this.openRow = null;
                } else {
                    this.onOpen(item);
                }
            }
        };

        if (onGrip) {
            event.preventDefault();
            main.setPointerCapture(event.pointerId);
        }
        window.addEventListener("pointermove", move);
        window.addEventListener("pointerup", finish);
        window.addEventListener("pointercancel", finish);
    }

    startDrag(li, event) {
        const rows = [...this.list.querySelectorAll(".row:not(.pending)")];
        const from = rows.indexOf(li);
        const drag = {
            li, rows, from, to: from,
            startY: event.clientY,
            startScroll: this.scroller.scrollTop,
            height: li.offsetHeight,
            centers: rows.map(row => row.offsetTop + row.offsetHeight / 2),
            pointerY: event.clientY,
            frame: 0,
        };
        li.classList.add("dragging");
        this.list.classList.add("sorting");
        const scroll = () => {
            const box = this.scroller.getBoundingClientRect();
            const edge = 48;
            let speed = 0;
            if (drag.pointerY < box.top + edge) speed = -Math.ceil((box.top + edge - drag.pointerY) / 4);
            else if (drag.pointerY > box.bottom - edge) speed = Math.ceil((drag.pointerY - box.bottom + edge) / 4);
            if (speed) {
                this.scroller.scrollTop += speed;
                this.dragTo(drag, drag.pointerY);
            }
            drag.frame = requestAnimationFrame(scroll);
        };
        drag.frame = requestAnimationFrame(scroll);
        if (navigator.vibrate) navigator.vibrate(8);
        return drag;
    }

    dragTo(drag, clientY) {
        drag.pointerY = clientY;
        const dy = clientY - drag.startY + (this.scroller.scrollTop - drag.startScroll);
        drag.li.style.transform = `translateY(${dy}px)`;
        const center = drag.centers[drag.from] + dy;
        let to = 0;
        drag.centers.forEach((value, i) => {
            if (i !== drag.from && value < center) to++;
        });
        drag.to = to;
        drag.rows.forEach((row, i) => {
            if (i === drag.from) return;
            let shift = 0;
            if (drag.from < to && i > drag.from && i <= to) shift = -drag.height;
            if (drag.from > to && i >= to && i < drag.from) shift = drag.height;
            row.style.transform = shift ? `translateY(${shift}px)` : "";
        });
    }

    endDrag(drag) {
        cancelAnimationFrame(drag.frame);
        this.list.classList.remove("sorting");
        drag.li.classList.remove("dragging");
        for (const row of drag.rows) row.style.transform = "";
        this.move(drag.from, drag.to);
    }
}

export function renderLibrary(list, articles, queuedIds, currentId, filter, { onOpen, onQueue }) {
    const query = filter.trim().toLocaleLowerCase();
    const rows = [];
    for (const item of articles) {
        if (query && !`${item.title} ${item.author} ${item.domain}`.toLocaleLowerCase().includes(query)) continue;
        const li = document.createElement("li");
        li.className = "row library";
        li.classList.toggle("current", item.id === currentId);
        li.classList.toggle("read", item.readState === 2);
        const main = document.createElement("button");
        main.type = "button";
        main.className = "row-main";
        const text = document.createElement("div");
        text.className = "row-text";
        const title = document.createElement("span");
        title.className = "row-title";
        title.textContent = item.title || "Untitled";
        const meta = document.createElement("span");
        meta.className = "row-meta";
        meta.textContent = describe(item);
        text.append(title, meta);
        main.append(text);
        main.addEventListener("click", () => onOpen(item));
        const queued = queuedIds.has(item.id);
        const add = document.createElement("button");
        add.type = "button";
        add.className = "row-add";
        add.disabled = queued;
        add.setAttribute("aria-label", queued ? "In the queue" : "Add to queue");
        add.append(icon(queued ? "icon-check" : "icon-plus"));
        add.addEventListener("click", () => onQueue(item));
        li.append(main, add);
        rows.push(li);
    }
    list.replaceChildren(...rows);
    return rows.length;
}
