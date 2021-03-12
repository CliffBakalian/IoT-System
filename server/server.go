package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"strconv"
	"time"
)

type pose struct {
	x float64
	y float64
	r float64
}

type cell struct {
	x int
	y int
}

/*
bot local IP may be different than net/http request remoteAddr
	but we need the bot IP to send communications to the bot
	and we need the bot remoteAddr to know who sends _us_ what
		actualy, remoteAddr may change! so, we require the bot send us
		it's ID at every POST
*/
var (
	bot       map[int]string     // int ID -> "ip-addr"
	remote    map[string]int     // "bot remote addr" -> int ID      TODO delete
	clocks    []int64            // [int ID] -> millisecond start time offset
	n	  int		=2
)

const (
	tone      int     = 300  // same as on ESP board
)

// registration received json
//  data returned from bot POSTing to us
type regPostData struct {
	Clock int64  `json:"clock,omitempty"`
	IP    string `json:"ip,omitempty"`
	Dev   string `json:"dev,omitempty"`
}

type movPostData struct {
	ID    int     `json:"id"`
	Start float64 `json:"start,omitempty"`
	End   float64 `json:"end,omitempty"`
	Rot   float64 `json:"rot,omitempty"`
	Mov   string  `json:"mov"`
}

// NOTING HERE -- rotation: + is left, - is right

var mov chan *movPostData

func makeTimestamp() int64 {
	return time.Now().UnixNano() / int64(time.Millisecond)
}

// server middleware for logging
func logging(logger *log.Logger) func(http.Handler) http.Handler {
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			// inject request id?
			requestID := r.Header.Get("X-Request-Id")
			if requestID == "" {
				requestID = fmt.Sprintf("%d", time.Now().UnixNano())
			}
			w.Header().Set("X-Request-Id", requestID)
			// log request
			defer func() {
				logger.Printf("[%v %v] <%v> %v (%v)\n", r.Method, r.URL.Path, r.RemoteAddr, r.UserAgent(), requestID)
			}()
			next.ServeHTTP(w, r)
		})
	}
}


type movCMD string

const (
	movForward  movCMD = "f"
	movBackward movCMD = "b"
	movRotate   movCMD = "r"
)

func doMovPost(c movCMD, l int, botID int) []byte {
	reqBody := []byte(fmt.Sprintf("%v,%d", c, l))
	resp, err := http.Post("http://"+bot[botID]+"/mov", "application/text", bytes.NewBuffer(reqBody))
	if err != nil {
		fmt.Printf(" doMovPost response error -- %v\n", err)
	}
	defer resp.Body.Close()
	body, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		fmt.Printf(" doMovPost body-read error -- %v\n", err)
	}
	return body
}

// *** MAIN SERVER ***

func main() {
	// OGM setup
	bot = make(map[int]string, 0) // num robots
	remote = make(map[string]int)
	clocks = make([]int64, 0)
	mov = make(chan *movPostData, n)

	// http setup
	log.Println("Starting server.")
	// option to run port on a given input argument
	port := 42
	if len(os.Args) > 1 {
		port, _ = strconv.Atoi(os.Args[1])
	}
	log.Printf("  server will run on port %v\n", port)

	// create logger
	logger := log.New(os.Stdout, "[ROUTER] ", log.LstdFlags)
	// create router
	router := http.NewServeMux()
	//   set up endpoint stop
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	// MAIN SERVER ENDPOINT HANDLERS
	router.HandleFunc("/end", func(w http.ResponseWriter, r *http.Request) {
		// w.Header().Set("Content-Type", "application/json")
		// print whatever statistics
		w.Write([]byte("server closed."))
		cancel()
	})
	router.HandleFunc("/reg", func(w http.ResponseWriter, r *http.Request) {
		t := makeTimestamp()
		log.Println("Someone wants to register...")
		switch r.Method {
		case "POST":
			reqBodyBytes, err := ioutil.ReadAll(r.Body)
			if err != nil {
				log.Fatal(err)
			}
			reqBody := &regPostData{}
			err = json.Unmarshal(reqBodyBytes, reqBody)
			if err != nil {
				log.Fatal(err)
			}
			log.Printf("  %v\n", reqBody)
			// see if ip has registered already
			newID := -1
			for id, ip := range bot {
				if ip == reqBody.IP {
					// robot has registered already
					newID = id
				}
			}
			if newID == -1 {
				// new robot
				newID = len(bot)
				newIP := reqBody.IP
				log.Printf("  %v -> %v\n", newID, newIP)
				bot = append(bot, newIP)
				remote[r.RemoteAddr] = newID
				clocks = append(clocks, t-reqBody.Clock) // move calculation up?
			}

			w.Write([]byte(strconv.Itoa(newID)))
		default:
			w.WriteHeader(http.StatusNotImplemented)
			w.Write([]byte(http.StatusText(http.StatusNotImplemented)))
		}
	})
	/*
		server will receive localization data from robots
		from speaker:
		- clock offset time when robot _started_ playing speaker

		from listener:
		- clock offset time when robot _started_ recording
		- integer array of amplitude samples from LEFT microphone
		- integer array of amplitude samples from RIGHT microphone

		then just ship these data into the localization channel (var loc chan *locPostData)
		which will be received by the localization thread
	*/
	router.HandleFunc("/mov", func(w http.ResponseWriter, r *http.Request) {
		switch r.Method {
		case "POST":
			reqBodyBytes, err := ioutil.ReadAll(r.Body)
			if err != nil {
				fmt.Println(err)
			}
			log.Println(string(reqBodyBytes))
			reqBody := &movPostData{}
			err = json.Unmarshal(reqBodyBytes, reqBody)
			if err != nil {
				fmt.Println(err)
			}
			mov <- reqBody
			w.Write([]byte(`thanks!`))
		default:
			w.WriteHeader(http.StatusNotImplemented)
			w.Write([]byte(http.StatusText(http.StatusNotImplemented)))
		}
	})
	router.HandleFunc("/debug", func(w http.ResponseWriter, r *http.Request) {
		reqBodyBytes, err := ioutil.ReadAll(r.Body)
		if err != nil {
			log.Println(err)
		} else {
			log.Println(string(reqBodyBytes))
		}
		w.Write([]byte(`thanks!`))
	})
	server := &http.Server{
		Addr:    ":" + strconv.Itoa(port),
		Handler: logging(logger)(router),
	}

	// spawn http server thread
	go func() {
		// always returns error. ErrServerClosed on graceful close
		log.Println("server listening...")
		if err := server.ListenAndServe(); err != http.ErrServerClosed {
			// unexpected error. port in use?
			log.Fatalf("ListenAndServe(): %v\n", err)
		}
	}()

	// block wait for shutdown
	select {
	case <-ctx.Done():
		// graceful shutdown
		if err := server.Shutdown(ctx); err != nil {
			// failure/timeout shutting down the server gracefully
			log.Println(err)
		}
	} // no default case needed

	log.Printf("server closed.")
}
